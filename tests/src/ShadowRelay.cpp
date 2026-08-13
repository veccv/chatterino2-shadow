// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/shadow/ShadowProtocol.hpp"

#include "Test.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

using namespace chatterino;
using namespace Qt::Literals;

TEST(ShadowProtocol, EncodeJoinLeavePublishHaveNoToken)
{
    auto join = encodeShadowJoin(u"123"_s);
    auto leave = encodeShadowLeave(u"123"_s);
    auto publish = encodeShadowPublish(u"123"_s, u"id-1"_s, u"hello"_s);

    EXPECT_FALSE(QString::fromUtf8(join).contains(u"oauth"_s,
                                                  Qt::CaseInsensitive));
    EXPECT_FALSE(QString::fromUtf8(leave).contains(u"oauth"_s,
                                                   Qt::CaseInsensitive));
    EXPECT_FALSE(QString::fromUtf8(publish).contains(u"oauth"_s,
                                                     Qt::CaseInsensitive));
    EXPECT_FALSE(QString::fromUtf8(publish).contains(u"token"_s,
                                                     Qt::CaseInsensitive));
}

TEST(ShadowProtocol, CompileTimeUrlHasNoQuery)
{
    QUrl url(u"ws://92.5.14.8:8787"_s);
    EXPECT_TRUE(url.query().isEmpty());
    EXPECT_FALSE(url.toString().contains(u"oauth"_s, Qt::CaseInsensitive));
}

TEST(ShadowProtocol, ParseHelloAndMessage)
{
    auto hello = parseShadowWire(R"({"op":"hello","login":"pajlada"})");
    ASSERT_TRUE(hello.has_value());
    EXPECT_EQ(hello->kind, ShadowWireEvent::Kind::Hello);
    EXPECT_EQ(hello->login, u"pajlada"_s);

    auto message = parseShadowWire(
        R"({"op":"message","room":"1","login":"pajlada","id":"abc","text":"still here"})");
    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(message->kind, ShadowWireEvent::Kind::Message);
    EXPECT_EQ(message->login, u"pajlada"_s);
    EXPECT_EQ(message->text, u"still here"_s);
    EXPECT_NE(message->login, u"spoofed"_s);
}

TEST(ShadowProtocol, RejectMalformedAndSpoofedHello)
{
    EXPECT_FALSE(parseShadowWire(R"(not json)").has_value());
    EXPECT_FALSE(parseShadowWire(R"({"op":"hello"})").has_value());
    EXPECT_FALSE(parseShadowWire(R"({"op":"message","login":"x"})").has_value());
}

TEST(ShadowProtocol, ParseAck)
{
    auto ack = parseShadowWire(R"({"op":"ack","id":"id-1"})");
    ASSERT_TRUE(ack.has_value());
    EXPECT_EQ(ack->kind, ShadowWireEvent::Kind::Ack);
    EXPECT_EQ(ack->id, u"id-1"_s);
}

TEST(ShadowProtocol, SuppressPendingSelfEcho)
{
    std::unordered_set<QString> pending{u"id-1"_s};
    auto incoming = parseShadowWire(
        R"({"op":"message","room":"1","login":"pajlada","id":"id-1","text":"hi"})");
    ASSERT_TRUE(incoming.has_value());
    EXPECT_TRUE(isPendingShadowEcho(incoming->id, pending));
    EXPECT_FALSE(isPendingShadowEcho(u"id-2"_s, pending));
}

TEST(ShadowProtocol, LiveOnlyHasNoHistoryField)
{
    auto message = parseShadowWire(
        R"({"op":"message","room":"1","login":"a","id":"1","text":"x"})");
    ASSERT_TRUE(message.has_value());
    EXPECT_TRUE(message->reason.isEmpty());
}

TEST(ShadowProtocol, EncodePublishKeepsEmojiAndEmoteName)
{
    auto publish = encodeShadowPublish(u"1"_s, u"id-1"_s, u"Kappa 😂 :tf:"_s);
    auto json = QJsonDocument::fromJson(publish).object();
    EXPECT_EQ(json.value(u"text"_s).toString(), u"Kappa 😂 :tf:"_s);
}

TEST(ShadowProtocol, EncodePublishIncludesColor)
{
    auto publish =
        encodeShadowPublish(u"1"_s, u"id-1"_s, u"Kappa"_s, u"#FF69B4"_s);
    auto json = QJsonDocument::fromJson(publish).object();
    EXPECT_EQ(json.value(u"color"_s).toString(), u"#FF69B4"_s);
}

TEST(ShadowProtocol, EncodePublishIncludesReply)
{
    auto publish = encodeShadowPublish(u"1"_s, u"id-2"_s, u"reply"_s,
                                       u"#FF69B4"_s, u"parent-1"_s);
    auto json = QJsonDocument::fromJson(publish).object();
    EXPECT_EQ(json.value(u"reply"_s).toString(), u"parent-1"_s);
}

TEST(ShadowProtocol, EncodePublishOmitsEmptyReply)
{
    auto publish = encodeShadowPublish(u"1"_s, u"id-1"_s, u"hello"_s);
    auto json = QJsonDocument::fromJson(publish).object();
    EXPECT_FALSE(json.contains(u"reply"_s));
}

TEST(ShadowProtocol, ParseMessageReply)
{
    auto message = parseShadowWire(
        R"({"op":"message","room":"1","login":"pajlada","id":"abc","text":"Kappa","reply":"parent-1"})");
    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(message->replyParentId, u"parent-1"_s);
    EXPECT_EQ(message->text, u"Kappa"_s);
}

TEST(ShadowProtocol, ParseMessageColor)
{
    auto message = parseShadowWire(
        R"({"op":"message","room":"1","login":"pajlada","id":"abc","text":"Kappa","color":"#FF69B4"})");
    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(message->color, u"#FF69B4"_s);
    EXPECT_EQ(message->text, u"Kappa"_s);
}
