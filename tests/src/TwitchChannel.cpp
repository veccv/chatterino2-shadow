// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"

#include "controllers/accounts/AccountController.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "messages/MessageFlag.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/Logging.hpp"
#include "mocks/TwitchIrcServer.hpp"
#include "Test.hpp"

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

    ILogging *getChatLogger() override
    {
        return &this->logging;
    }

    AccountController accounts;
    mock::MockTwitchIrcServer twitch;
    mock::EmptyLogging logging;
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
        [&](const QString & /*msg*/, bool & /*sent*/) {
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
        [&](const QString & /*msg*/, bool &sent) {
            helixCalled = true;
            sent = true;
        });

    bool sent = false;
    ASSERT_TRUE(this->channel->tryInterceptShadowSend("timed out", sent));
    ASSERT_FALSE(helixCalled);

    this->channel->setRestriction(ChannelRestriction::None);
    sent = false;
    ASSERT_FALSE(this->channel->tryInterceptShadowSend("back", sent));
    this->channel->sendMessageSignal.invoke("back", sent);
    ASSERT_TRUE(helixCalled);
    ASSERT_TRUE(sent);
}

TEST_F(TwitchChannelRestriction, NoneDoesNotPublishToShadow)
{
    bool helixCalled = false;
    std::ignore = this->channel->sendMessageSignal.connect(
        [&](const QString & /*msg*/, bool &sent) {
            helixCalled = true;
            sent = true;
        });

    bool sent = false;
    ASSERT_FALSE(this->channel->tryInterceptShadowSend("hello", sent));
    ASSERT_FALSE(sent);
    this->channel->sendMessageSignal.invoke("hello", sent);
    ASSERT_TRUE(helixCalled);
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
