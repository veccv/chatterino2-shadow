// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"

#include "controllers/accounts/AccountController.hpp"
#include "messages/Emote.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "messages/MessageFlag.hpp"
#include "messages/MessageThread.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/EmoteController.hpp"
#include "mocks/Logging.hpp"
#include "mocks/TwitchIrcServer.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/ffz/FfzEmotes.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "Test.hpp"

#include <QColor>
#include <QString>
#include <QtCore/qtestsupport_core.h>

#include <memory>
#include <vector>

using namespace chatterino;

namespace {

class MockApplication : public mock::BaseApplication
{
public:
    AccountController *getAccounts() override
    {
        return &this->accounts;
    }

    ITwitchIrcServer *getTwitch() override
    {
        return &this->twitch;
    }

    EmoteController *getEmotes() override
    {
        return &this->emotes;
    }

    BttvEmotes *getBttvEmotes() override
    {
        return &this->bttvEmotes;
    }

    FfzEmotes *getFfzEmotes() override
    {
        return &this->ffzEmotes;
    }

    SeventvEmotes *getSeventvEmotes() override
    {
        return &this->seventvEmotes;
    }

    ILogging *getChatLogger() override
    {
        return &this->logging;
    }

    AccountController accounts;
    mock::MockTwitchIrcServer twitch;
    mock::EmptyLogging logging;
    mock::EmoteController emotes;
    BttvEmotes bttvEmotes;
    FfzEmotes ffzEmotes;
    SeventvEmotes seventvEmotes;
};

class TwitchChannelRestriction : public ::testing::Test
{
protected:
    void SetUp() override
    {
        this->app = std::make_unique<MockApplication>();
        this->channel = std::make_shared<TwitchChannel>("pajlada");
    }

    void TearDown() override
    {
        this->channel.reset();
        this->app.reset();
    }

    std::unique_ptr<MockApplication> app;
    std::shared_ptr<TwitchChannel> channel;
};

}  // namespace

namespace chatterino::detail {

TEST(TwitchChannelDetail_isUnknownCommand, good)
{
    // clang-format off
    std::vector<QString> cases{
        "/me hello",
        ".me hello",
        "/ hello",
        ". hello",
        "/ /hello",
        ". .hello",
        "/ .hello",
        ". /hello",
        ".", // this results in an empty message but not in an error (twitchdev/issues#1019)
        "/me",
        ".me",
        "..",
        "...",
        "....",
        "",
        "foo",
        "a",
        "!",
        ". .",
        ". ..",
        ".. ..",
        ".. .",
        "/ /",
        "/ .",
        ". /",
        ". ./",
        ".. /",
        ".. me",
        ". me",
    };
    // clang-format on

    for (const auto &input : cases)
    {
        ASSERT_FALSE(isUnknownCommand(input))
            << input << " should not be considered an unknown command";
    }
}

TEST(TwitchChannelDetail_isUnknownCommand, bad)
{
    // clang-format off
    std::vector<QString> cases{
        "/badcommand",
        ".badcommand",
        "/badcommand hello",
        ".badcommand hello",
        "/@badcommand hello",
        ".@badcommand hello",
        "/bann username ban reason",
        "/bann username",
        "//",
        "./",
        "./me",
        "./w",
        "/.",
        "/.me",
        "/.w",
        "/,me",
    };
    // clang-format on

    for (const auto &input : cases)
    {
        ASSERT_TRUE(isUnknownCommand(input))
            << input << " should be considered an unknown command";
    }
}

}  // namespace chatterino::detail

TEST_F(TwitchChannelRestriction, DefaultIsNone)
{
    ASSERT_EQ(this->channel->restriction(), ChannelRestriction::None);
}

TEST_F(TwitchChannelRestriction, SlowModeSendWaitDoesNotSetTimedOut)
{
    this->channel->setSendWait(30);
    ASSERT_EQ(this->channel->restriction(), ChannelRestriction::None);
}

TEST_F(TwitchChannelRestriction, FollowersOnlyStaysNone)
{
    // Helix Forbidden for followers-only is not a restriction signal.
    ASSERT_EQ(this->channel->restriction(), ChannelRestriction::None);
    this->channel->setSendWait(0);
    ASSERT_EQ(this->channel->restriction(), ChannelRestriction::None);
}

TEST_F(TwitchChannelRestriction, PermabanSetsBanned)
{
    int fires = 0;
    std::ignore = this->channel->restrictionChanged.connect([&] {
        fires++;
    });

    this->channel->setRestriction(ChannelRestriction::Banned);
    ASSERT_EQ(this->channel->restriction(), ChannelRestriction::Banned);
    ASSERT_EQ(fires, 1);

    this->channel->setRestriction(ChannelRestriction::Banned);
    ASSERT_EQ(fires, 1);
}

TEST_F(TwitchChannelRestriction, TimedOutClearsAfterWait)
{
    this->channel->setTimedOut(1);
    ASSERT_EQ(this->channel->restriction(), ChannelRestriction::TimedOut);

    QTest::qWait(1500);
    ASSERT_EQ(this->channel->restriction(), ChannelRestriction::None);
}

TEST_F(TwitchChannelRestriction, TimedOutDoesNotClearSendWait)
{
    this->channel->setSendWait(30);
    this->channel->setTimedOut(1);
    ASSERT_EQ(this->channel->restriction(), ChannelRestriction::TimedOut);

    QTest::qWait(1500);
    ASSERT_EQ(this->channel->restriction(), ChannelRestriction::None);
}

TEST_F(TwitchChannelRestriction, BannedStopsTimeoutTimer)
{
    this->channel->setTimedOut(5);
    this->channel->setRestriction(ChannelRestriction::Banned);
    ASSERT_EQ(this->channel->restriction(), ChannelRestriction::Banned);

    QTest::qWait(100);
    ASSERT_EQ(this->channel->restriction(), ChannelRestriction::Banned);
}

TEST_F(TwitchChannelRestriction, BannedSendDoesNotInvokeTwitch)
{
    this->channel->setRestriction(ChannelRestriction::Banned);

    bool helixCalled = false;
    std::ignore = this->channel->sendMessageSignal.connect(
        [&](const QString & /*msg*/, bool & /*sent*/, bool /*fallback*/) {
            helixCalled = true;
        });

    bool sent = false;
    ASSERT_TRUE(this->channel->tryInterceptShadowSend("still here", sent));
    ASSERT_FALSE(sent);
    ASSERT_FALSE(helixCalled);

    auto messages = this->channel->getMessageSnapshot();
    ASSERT_FALSE(messages.empty());
    ASSERT_TRUE(messages.back()->flags.has(MessageFlag::System));
}

TEST_F(TwitchChannelRestriction, TimedOutSendGoesToShadowThenTwitch)
{
    this->channel->setRestriction(ChannelRestriction::TimedOut);

    bool helixCalled = false;
    std::ignore = this->channel->sendMessageSignal.connect(
        [&](const QString & /*msg*/, bool &sent, bool /*fallback*/) {
            helixCalled = true;
            sent = true;
        });

    bool sent = false;
    ASSERT_TRUE(this->channel->tryInterceptShadowSend("timed out", sent));
    ASSERT_FALSE(helixCalled);

    this->channel->setRestriction(ChannelRestriction::None);
    sent = false;
    ASSERT_FALSE(this->channel->tryInterceptShadowSend("back", sent));
    this->channel->sendMessageSignal.invoke("back", sent, true);
    ASSERT_TRUE(helixCalled);
    ASSERT_TRUE(sent);
}

TEST_F(TwitchChannelRestriction, NoneDoesNotPublishToShadow)
{
    bool helixCalled = false;
    std::ignore = this->channel->sendMessageSignal.connect(
        [&](const QString & /*msg*/, bool &sent, bool /*fallback*/) {
            helixCalled = true;
            sent = true;
        });

    bool sent = false;
    ASSERT_FALSE(this->channel->tryInterceptShadowSend("hello", sent));
    ASSERT_FALSE(sent);
    this->channel->sendMessageSignal.invoke("hello", sent, true);
    ASSERT_TRUE(helixCalled);
}

TEST_F(TwitchChannelRestriction, NormalTargetSkipsShadowWhenBanned)
{
    this->channel->setRestriction(ChannelRestriction::Banned);

    bool helixCalled = false;
    std::ignore = this->channel->sendMessageSignal.connect(
        [&](const QString & /*msg*/, bool & /*sent*/, bool /*fallback*/) {
            helixCalled = true;
        });

    bool sent = false;
    ASSERT_FALSE(this->channel->tryInterceptShadowSend(
        "still here", sent, ShadowSendTarget::Normal));
    ASSERT_FALSE(sent);
    ASSERT_FALSE(helixCalled);
}

TEST_F(TwitchChannelRestriction, ShadowTargetInterceptsWhenUnrestricted)
{
    bool helixCalled = false;
    std::ignore = this->channel->sendMessageSignal.connect(
        [&](const QString & /*msg*/, bool & /*sent*/, bool /*fallback*/) {
            helixCalled = true;
        });

    bool sent = false;
    ASSERT_TRUE(this->channel->tryInterceptShadowSend(
        "hello", sent, ShadowSendTarget::Shadow));
    ASSERT_FALSE(sent);
    ASSERT_FALSE(helixCalled);

    auto messages = this->channel->getMessageSnapshot();
    ASSERT_FALSE(messages.empty());
    ASSERT_TRUE(messages.back()->flags.has(MessageFlag::System));
}

TEST_F(TwitchChannelRestriction, IncomingShadowLineIsMarked)
{
    this->channel->addShadowChatLine("pajlada", "still here");

    auto messages = this->channel->getMessageSnapshot();
    ASSERT_EQ(messages.size(), 1);
    ASSERT_TRUE(messages[0]->flags.has(MessageFlag::ShadowMessage));
    ASSERT_EQ(messages[0]->loginName, "pajlada");
    ASSERT_EQ(messages[0]->messageText, "still here");
}

TEST_F(TwitchChannelRestriction, ShadowLineUsesBadgeInsteadOfText)
{
    this->channel->addShadowChatLine("pajlada", "still here");

    auto messages = this->channel->getMessageSnapshot();
    ASSERT_EQ(messages.size(), 1);

    bool foundShadowBadge = false;
    bool foundShadowText = false;
    for (const auto &element : messages[0]->elements)
    {
        if (dynamic_cast<const BadgeElement *>(element.get()) != nullptr &&
            element->getTooltip() == QStringLiteral("Shadow user"))
        {
            foundShadowBadge = true;
        }
        auto *text = dynamic_cast<const TextElement *>(element.get());
        if (text != nullptr &&
            text->words().contains(QStringLiteral("[shadow]")))
        {
            foundShadowText = true;
        }
    }
    EXPECT_TRUE(foundShadowBadge);
    EXPECT_FALSE(foundShadowText);
    EXPECT_TRUE(
        messages[0]->searchText.startsWith(QStringLiteral("Shadow user ")));
}

TEST_F(TwitchChannelRestriction, ShadowLineUsesNickColor)
{
    this->channel->addShadowChatLine("pajlada", "Kappa", QColor("#FF69B4"));

    auto messages = this->channel->getMessageSnapshot();
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages[0]->usernameColor, QColor("#FF69B4"));
}

TEST_F(TwitchChannelRestriction, ShadowLineRendersNamedEmote)
{
    auto map = std::make_shared<EmoteMap>();
    EmoteName name{QStringLiteral("Kappa")};
    map->emplace(name, std::make_shared<Emote>(Emote{.name = name}));
    this->channel->setBttvEmotes(std::shared_ptr<const EmoteMap>(map));
    this->channel->addShadowChatLine("pajlada", "hello Kappa");

    auto messages = this->channel->getMessageSnapshot();
    ASSERT_EQ(messages.size(), 1);
    bool foundEmote = false;
    for (const auto &element : messages[0]->elements)
    {
        if (dynamic_cast<const EmoteElement *>(element.get()) != nullptr)
        {
            foundEmote = true;
            break;
        }
    }
    EXPECT_TRUE(foundEmote);
}

TEST_F(TwitchChannelRestriction, ShadowLineRendersColonWrappedEmote)
{
    auto map = std::make_shared<EmoteMap>();
    EmoteName name{QStringLiteral("Kappa")};
    map->emplace(name, std::make_shared<Emote>(Emote{.name = name}));
    this->channel->setBttvEmotes(std::shared_ptr<const EmoteMap>(map));
    this->channel->addShadowChatLine("pajlada", ":Kappa:");

    auto messages = this->channel->getMessageSnapshot();
    ASSERT_EQ(messages.size(), 1);
    bool foundEmote = false;
    for (const auto &element : messages[0]->elements)
    {
        if (dynamic_cast<const EmoteElement *>(element.get()) != nullptr)
        {
            foundEmote = true;
            break;
        }
    }
    EXPECT_TRUE(foundEmote);
}

TEST_F(TwitchChannelRestriction, ShadowLineRendersMentionAndEmote)
{
    auto map = std::make_shared<EmoteMap>();
    EmoteName name{QStringLiteral("FeelsWeirdMan")};
    map->emplace(name, std::make_shared<Emote>(Emote{.name = name}));
    this->channel->setBttvEmotes(std::shared_ptr<const EmoteMap>(map));
    this->channel->addShadowChatLine("pajlada", "@forsen FeelsWeirdMan");

    auto messages = this->channel->getMessageSnapshot();
    ASSERT_EQ(messages.size(), 1);
    bool foundEmote = false;
    bool foundMention = false;
    for (const auto &element : messages[0]->elements)
    {
        if (dynamic_cast<const EmoteElement *>(element.get()) != nullptr)
        {
            foundEmote = true;
        }
        if (dynamic_cast<const MentionElement *>(element.get()) != nullptr)
        {
            foundMention = true;
        }
    }
    EXPECT_TRUE(foundEmote);
    EXPECT_TRUE(foundMention);
}

TEST_F(TwitchChannelRestriction, BannedArmsGhostWatch)
{
    this->channel->setRestriction(ChannelRestriction::Banned);
    ASSERT_TRUE(this->app->twitch.ghostArmed.contains("pajlada"));
    ASSERT_EQ(this->app->accounts.twitch.getCurrent()->getUserName(),
              "justinfan64537");

    this->channel->setRestriction(ChannelRestriction::None);
    ASSERT_FALSE(this->app->twitch.ghostArmed.contains("pajlada"));
}

TEST_F(TwitchChannelRestriction, GhostWatchLineIsNotShadow)
{
    this->channel->setRestriction(ChannelRestriction::Banned);
    auto builder = MessageBuilder();
    builder.emplace<TextElement>("hello", MessageElementFlag::Text);
    builder->messageText = "hello";
    auto msg = builder.release();
    this->channel->addMessage(msg, MessageContext::Original);

    ASSERT_FALSE(msg->flags.has(MessageFlag::ShadowMessage));
}

TEST_F(TwitchChannelRestriction, ShadowLineGetsReplyableId)
{
    this->channel->addShadowChatLine("pajlada", "still here", {},
                                     QStringLiteral("shadow-1"));

    auto messages = this->channel->getMessageSnapshot();
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages[0]->id, QStringLiteral("shadow-1"));
    EXPECT_EQ(messages[0]->isReplyable(), Message::ReplyStatus::Replyable);
}

TEST_F(TwitchChannelRestriction, ShadowReplyJoinsTwitchThread)
{
    auto root = std::make_shared<Message>();
    root->id = QStringLiteral("twitch-1");
    root->loginName = QStringLiteral("forsen");
    root->displayName = QStringLiteral("forsen");
    root->messageText = QStringLiteral("hello twitch");
    this->channel->addMessage(root, MessageContext::Original);

    this->channel->addShadowChatLine("pajlada", "from shadow", {},
                                     QStringLiteral("shadow-1"),
                                     QStringLiteral("twitch-1"));

    auto messages = this->channel->getMessageSnapshot();
    ASSERT_EQ(messages.size(), 2);
    ASSERT_TRUE(messages[1]->flags.has(MessageFlag::ReplyMessage));
    ASSERT_NE(messages[1]->replyThread, nullptr);
    EXPECT_EQ(messages[1]->replyThread->rootId(), QStringLiteral("twitch-1"));
    ASSERT_NE(messages[1]->replyParent, nullptr);
    EXPECT_EQ(messages[1]->replyParent->id, QStringLiteral("twitch-1"));
}

TEST_F(TwitchChannelRestriction, ShadowReplyBadgeSitsWithUsername)
{
    auto root = std::make_shared<Message>();
    root->id = QStringLiteral("twitch-1");
    root->loginName = QStringLiteral("forsen");
    root->displayName = QStringLiteral("forsen");
    root->messageText = QStringLiteral("hello twitch");
    this->channel->addMessage(root, MessageContext::Original);

    this->channel->addShadowChatLine("pajlada", "from shadow", {},
                                     QStringLiteral("shadow-1"),
                                     QStringLiteral("twitch-1"));

    auto messages = this->channel->getMessageSnapshot();
    ASSERT_EQ(messages.size(), 2);

    const auto &elements = messages[1]->elements;
    auto indexOf = [&](auto pred) -> int {
        for (int i = 0; i < static_cast<int>(elements.size()); ++i)
        {
            if (pred(elements[static_cast<size_t>(i)].get()))
            {
                return i;
            }
        }
        return -1;
    };

    const int reply = indexOf([](const MessageElement *e) {
        return dynamic_cast<const ReplyCurveElement *>(e) != nullptr;
    });
    const int timestamp = indexOf([](const MessageElement *e) {
        return dynamic_cast<const TimestampElement *>(e) != nullptr;
    });
    const int badge = indexOf([](const MessageElement *e) {
        return dynamic_cast<const BadgeElement *>(e) != nullptr &&
               e->getTooltip() == QStringLiteral("Shadow user");
    });
    const int nick = indexOf([](const MessageElement *e) {
        auto *text = dynamic_cast<const TextElement *>(e);
        return text != nullptr &&
               text->getFlags().has(MessageElementFlag::Username);
    });

    ASSERT_GE(reply, 0);
    ASSERT_GE(timestamp, 0);
    ASSERT_GE(badge, 0);
    ASSERT_GE(nick, 0);
    EXPECT_LT(reply, timestamp);
    EXPECT_LT(timestamp, badge);
    EXPECT_EQ(badge + 1, nick);
}

TEST_F(TwitchChannelRestriction, ShadowUsersCanThreadAmongThemselves)
{
    this->channel->addShadowChatLine("pajlada", "root", {},
                                     QStringLiteral("shadow-root"));
    this->channel->addShadowChatLine("other", "reply", {},
                                     QStringLiteral("shadow-reply"),
                                     QStringLiteral("shadow-root"));
    this->channel->addShadowChatLine("pajlada", "again", {},
                                     QStringLiteral("shadow-reply-2"),
                                     QStringLiteral("shadow-reply"));

    auto messages = this->channel->getMessageSnapshot();
    ASSERT_EQ(messages.size(), 3);
    ASSERT_NE(messages[1]->replyThread, nullptr);
    EXPECT_EQ(messages[1]->replyThread->rootId(),
              QStringLiteral("shadow-root"));
    ASSERT_NE(messages[2]->replyThread, nullptr);
    EXPECT_EQ(messages[2]->replyThread.get(), messages[1]->replyThread.get());
    ASSERT_NE(messages[2]->replyParent, nullptr);
    EXPECT_EQ(messages[2]->replyParent->id, QStringLiteral("shadow-reply"));
}
