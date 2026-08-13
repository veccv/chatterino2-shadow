// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <optional>
#include <unordered_set>

namespace chatterino {

struct ShadowWireEvent {
    enum class Kind {
        Hello,
        Ack,
        Message,
        Error,
        Unknown,
    };

    Kind kind = Kind::Unknown;
    QString login;
    QString roomId;
    QString id;
    QString text;
    QString color;
    QString reason;
};

inline QByteArray encodeShadowJoin(const QString &roomId)
{
    QJsonObject root;
    root.insert(QStringLiteral("op"), QStringLiteral("join"));
    root.insert(QStringLiteral("room"), roomId);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

inline QByteArray encodeShadowLeave(const QString &roomId)
{
    QJsonObject root;
    root.insert(QStringLiteral("op"), QStringLiteral("leave"));
    root.insert(QStringLiteral("room"), roomId);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

inline QByteArray encodeShadowPublish(const QString &roomId, const QString &id,
                                      const QString &text,
                                      const QString &color = {})
{
    QJsonObject root;
    root.insert(QStringLiteral("op"), QStringLiteral("publish"));
    root.insert(QStringLiteral("room"), roomId);
    root.insert(QStringLiteral("id"), id);
    root.insert(QStringLiteral("text"), text);
    if (!color.isEmpty())
    {
        root.insert(QStringLiteral("color"), color);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

inline std::optional<ShadowWireEvent> parseShadowWire(const QByteArray &data)
{
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
    {
        return std::nullopt;
    }

    auto root = doc.object();
    auto op = root.value(QStringLiteral("op")).toString();
    ShadowWireEvent event;

    if (op == QStringLiteral("hello"))
    {
        event.kind = ShadowWireEvent::Kind::Hello;
        event.login = root.value(QStringLiteral("login")).toString();
        if (event.login.isEmpty())
        {
            return std::nullopt;
        }
        return event;
    }
    if (op == QStringLiteral("ack"))
    {
        event.kind = ShadowWireEvent::Kind::Ack;
        event.id = root.value(QStringLiteral("id")).toString();
        if (event.id.isEmpty())
        {
            return std::nullopt;
        }
        return event;
    }
    if (op == QStringLiteral("message"))
    {
        event.kind = ShadowWireEvent::Kind::Message;
        event.roomId = root.value(QStringLiteral("room")).toString();
        event.login = root.value(QStringLiteral("login")).toString();
        event.id = root.value(QStringLiteral("id")).toString();
        event.text = root.value(QStringLiteral("text")).toString();
        event.color = root.value(QStringLiteral("color")).toString();
        if (event.roomId.isEmpty() || event.login.isEmpty() ||
            event.id.isEmpty())
        {
            return std::nullopt;
        }
        return event;
    }
    if (op == QStringLiteral("error"))
    {
        event.kind = ShadowWireEvent::Kind::Error;
        event.reason = root.value(QStringLiteral("reason")).toString();
        return event;
    }

    event.kind = ShadowWireEvent::Kind::Unknown;
    return event;
}

inline bool isPendingShadowEcho(const QString &id,
                                const std::unordered_set<QString> &pendingIds)
{
    return pendingIds.contains(id);
}

}  // namespace chatterino
