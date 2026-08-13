// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/shadow/ShadowRelay.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "util/PostToThread.hpp"

#include <QPointer>
#include <QUrl>

namespace chatterino {

class ShadowRelay::Listener : public WebSocketListener
{
public:
    Listener(ShadowRelay *relay, int generation)
        : relay_(relay)
        , generation_(generation)
    {
    }

    void onOpen() override
    {
        auto *relay = this->relay_.data();
        if (relay == nullptr)
        {
            return;
        }
        postToThread(
            [relay = QPointer<ShadowRelay>(relay), gen = this->generation_] {
                if (relay)
                {
                    relay->handleOpen(gen);
                }
            },
            relay);
    }

    void onTextMessage(QByteArray data) override
    {
        auto *relay = this->relay_.data();
        if (relay == nullptr)
        {
            return;
        }
        postToThread(
            [relay = QPointer<ShadowRelay>(relay), gen = this->generation_,
             data = std::move(data)] {
                if (relay)
                {
                    relay->handleText(gen, data);
                }
            },
            relay);
    }

    void onBinaryMessage(QByteArray /*data*/) override
    {
    }

    void onClose(std::unique_ptr<WebSocketListener> self) override
    {
        auto *relay = this->relay_.data();
        if (relay == nullptr)
        {
            return;
        }
        postToThread(
            [relay = QPointer<ShadowRelay>(relay), gen = this->generation_,
             keepalive = std::move(self)]() mutable {
                (void)keepalive;
                if (relay)
                {
                    relay->handleClose(gen);
                }
            },
            relay);
    }

private:
    QPointer<ShadowRelay> relay_;
    int generation_;
};

ShadowRelay::ShadowRelay(QString host)
    : host_(std::move(host))
    , pool_(std::in_place, QStringLiteral("shadow"))
{
    this->reconnectTimer_.setSingleShot(true);
    QObject::connect(&this->reconnectTimer_, &QTimer::timeout, this, [this] {
        this->connect();
    });
    this->signalHolder_.managedConnect(
        getApp()->getAccounts()->twitch.currentUserChanged, [this] {
            this->onCurrentUserChanged();
        });
}

ShadowRelay::~ShadowRelay()
{
    this->stop();
}

void ShadowRelay::stop()
{
    this->stopping_ = true;
    this->generation_++;
    this->reconnectTimer_.stop();
    this->socket_ = {};
    this->pool_.reset();
    this->open_ = false;
    this->authenticating_ = false;
    this->validatedLogin_.clear();
}

void ShadowRelay::subscribeChannel(const QString &roomId)
{
    assertInGuiThread();
    if (roomId.isEmpty() || this->stopping_)
    {
        return;
    }

    auto [it, inserted] = this->rooms_.insert(roomId);
    (void)it;
    this->ensureConnected();
    if (inserted && this->isAuthenticated())
    {
        this->sendJoin(roomId);
    }
}

void ShadowRelay::unsubscribeChannel(const QString &roomId)
{
    assertInGuiThread();
    if (this->rooms_.erase(roomId) == 0)
    {
        return;
    }
    if (this->isAuthenticated())
    {
        this->socket_.sendText(encodeShadowLeave(roomId));
    }
}

bool ShadowRelay::publish(const QString &roomId, const QString &text,
                          const QString &id, const QString &color,
                          const QString &replyParentId)
{
    assertInGuiThread();
    if (!this->isAuthenticated() || roomId.isEmpty() || text.isEmpty() ||
        id.isEmpty())
    {
        return false;
    }

    this->pendingIds_.insert(id);
    this->socket_.sendText(
        encodeShadowPublish(roomId, id, text, color, replyParentId));
    return true;
}

bool ShadowRelay::isAuthenticated() const
{
    return this->open_ && !this->validatedLogin_.isEmpty();
}

ShadowConnectionState ShadowRelay::connectionState() const
{
    if (this->isAuthenticated())
    {
        return ShadowConnectionState::Connected;
    }
    if (this->authenticating_ || this->open_)
    {
        return ShadowConnectionState::Connecting;
    }
    return ShadowConnectionState::Disconnected;
}

QString ShadowRelay::validatedLogin() const
{
    return this->validatedLogin_;
}

void ShadowRelay::ensureConnected()
{
    if (this->open_ || this->authenticating_ || this->stopping_)
    {
        return;
    }
    if (this->rooms_.empty())
    {
        return;
    }
    this->connect();
}

void ShadowRelay::onCurrentUserChanged()
{
    assertInGuiThread();
    this->reconnectTimer_.stop();
    this->reconnectBackoff_.reset();
    this->resetSocket();
    this->ensureConnected();
}

void ShadowRelay::resetSocket()
{
    this->generation_++;
    const bool wasAuthenticated = this->isAuthenticated();
    this->open_ = false;
    this->authenticating_ = false;
    this->validatedLogin_.clear();
    this->pendingIds_.clear();
    this->socket_.close();
    if (wasAuthenticated)
    {
        this->disconnected.invoke();
    }
    this->connectionStateChanged.invoke();
}

void ShadowRelay::connect()
{
    assertInGuiThread();
    if (this->stopping_ || !this->pool_)
    {
        return;
    }

    auto account = getApp()->getAccounts()->twitch.getCurrent();
    if (account->isAnon() || account->getOAuthToken().isEmpty())
    {
        qCDebug(chatterinoShadow) << "Shadow relay skipped: no OAuth account";
        return;
    }

    QUrl url(this->host_);
    if (!url.query().isEmpty())
    {
        qCWarning(chatterinoShadow)
            << "Shadow relay URL must not contain a query string";
        return;
    }

    this->generation_++;
    const auto generation = this->generation_;
    this->authenticating_ = true;
    this->validatedLogin_.clear();
    this->socket_.close();

    WebSocketOptions options;
    options.url = url;
    options.headers.emplace_back(
        "Authorization",
        QStringLiteral("OAuth %1").arg(account->getOAuthToken()).toStdString());

    qCDebug(chatterinoShadow) << "Connecting shadow relay";
    this->socket_ = this->pool_->createSocket(
        std::move(options), std::make_unique<Listener>(this, generation));
    this->connectionStateChanged.invoke();
}

void ShadowRelay::handleOpen(int generation)
{
    if (generation != this->generation_)
    {
        return;
    }
    this->open_ = true;
    this->reconnectBackoff_.reset();
}

void ShadowRelay::handleText(int generation, const QByteArray &data)
{
    if (generation != this->generation_)
    {
        return;
    }
    auto event = parseShadowWire(data);
    if (!event)
    {
        qCDebug(chatterinoShadow) << "Ignoring malformed shadow event";
        return;
    }

    switch (event->kind)
    {
        case ShadowWireEvent::Kind::Hello:
            this->authenticating_ = false;
            this->validatedLogin_ = event->login;
            qCDebug(chatterinoShadow)
                << "Shadow relay authenticated as" << this->validatedLogin_;
            this->authenticated.invoke();
            this->connectionStateChanged.invoke();
            for (const auto &room : this->rooms_)
            {
                this->sendJoin(room);
            }
            break;
        case ShadowWireEvent::Kind::Ack: {
            this->pendingIds_.erase(event->id);
            this->publishAck.invoke(event->id);
            break;
        }
        case ShadowWireEvent::Kind::Message:
            if (isPendingShadowEcho(event->id, this->pendingIds_))
            {
                this->pendingIds_.erase(event->id);
                break;
            }
            this->messageReceived.invoke(*event);
            break;
        case ShadowWireEvent::Kind::Error:
            qCWarning(chatterinoShadow)
                << "Shadow relay error:" << event->reason;
            break;
        case ShadowWireEvent::Kind::Unknown:
            break;
    }
}

void ShadowRelay::handleClose(int generation)
{
    if (generation != this->generation_)
    {
        return;
    }
    bool wasOpen = this->open_;
    this->open_ = false;
    this->authenticating_ = false;
    this->validatedLogin_.clear();
    this->pendingIds_.clear();
    this->disconnected.invoke();
    this->connectionStateChanged.invoke();
    if (wasOpen)
    {
        qCDebug(chatterinoShadow) << "Shadow relay disconnected";
    }
    this->scheduleReconnect();
}

void ShadowRelay::sendJoin(const QString &roomId)
{
    this->socket_.sendText(encodeShadowJoin(roomId));
}

void ShadowRelay::scheduleReconnect()
{
    if (this->stopping_ || this->rooms_.empty())
    {
        return;
    }
    auto delay = this->reconnectBackoff_.next();
    this->reconnectTimer_.start(delay);
}

}  // namespace chatterino
