// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/websockets/WebSocketPool.hpp"
#include "providers/shadow/ShadowProtocol.hpp"
#include "util/ExponentialBackoff.hpp"

#include <pajlada/signals/signal.hpp>
#include <pajlada/signals/signalholder.hpp>
#include <QObject>
#include <QString>
#include <QTimer>

#include <optional>
#include <unordered_set>

namespace chatterino {

class ShadowRelay : public QObject
{
    Q_OBJECT

public:
    explicit ShadowRelay(QString host);
    ~ShadowRelay() override;

    ShadowRelay(const ShadowRelay &) = delete;
    ShadowRelay(ShadowRelay &&) = delete;
    ShadowRelay &operator=(const ShadowRelay &) = delete;
    ShadowRelay &operator=(ShadowRelay &&) = delete;

    void subscribeChannel(const QString &roomId);
    void unsubscribeChannel(const QString &roomId);

    /// Publish text to a room. Returns false if the socket is not authenticated.
    bool publish(const QString &roomId, const QString &text, const QString &id,
                 const QString &color = {});

    pajlada::Signals::Signal<ShadowWireEvent> messageReceived;
    pajlada::Signals::Signal<QString> publishAck;
    pajlada::Signals::NoArgSignal authenticated;
    pajlada::Signals::NoArgSignal disconnected;

    void stop();

    bool isAuthenticated() const;
    QString validatedLogin() const;

private:
    class Listener;

    void ensureConnected();
    void connect();
    void onCurrentUserChanged();
    void resetSocket();
    void handleOpen(int generation);
    void handleText(int generation, const QByteArray &data);
    void handleClose(int generation);
    void sendJoin(const QString &roomId);
    void scheduleReconnect();

    QString host_;
    std::optional<WebSocketPool> pool_;
    WebSocketHandle socket_;
    std::unordered_set<QString> rooms_;
    std::unordered_set<QString> pendingIds_;
    QString validatedLogin_;
    bool open_ = false;
    bool authenticating_ = false;
    bool stopping_ = false;
    int generation_ = 0;
    ExponentialBackoff<6> reconnectBackoff_{std::chrono::milliseconds(500)};
    QTimer reconnectTimer_;
    pajlada::Signals::SignalHolder signalHolder_;
};

}  // namespace chatterino
