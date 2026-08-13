// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/splits/SplitInput.hpp"

#include "common/Literals.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/Command.hpp"
#include "controllers/commands/CommandController.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/EmoteController.hpp"
#include "providers/shadow/ShadowRelay.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "singletons/WindowManager.hpp"
#include "Test.hpp"
#include "widgets/Notebook.hpp"
#include "widgets/splits/Split.hpp"

#include <QDebug>
#include <QString>

using namespace chatterino;
using ::testing::Exactly;

namespace {

class MockApplication : public mock::BaseApplication
{
public:
    MockApplication()
        : windowManager(this->args_, this->paths_, this->settings, this->theme,
                        this->fonts)
        , commands(this->paths_)
    {
    }

    HotkeyController *getHotkeys() override
    {
        return &this->hotkeys;
    }

    WindowManager *getWindows() override
    {
        return &this->windowManager;
    }

    AccountController *getAccounts() override
    {
        return &this->accounts;
    }

    CommandController *getCommands() override
    {
        return &this->commands;
    }

    EmoteController *getEmotes() override
    {
        return &this->emotes;
    }

    HotkeyController hotkeys;
    WindowManager windowManager;
    AccountController accounts;
    CommandController commands;
    mock::EmoteController emotes;
};

class SplitInputTest
    : public ::testing::TestWithParam<std::tuple<QString, QString>>
{
public:
    SplitInputTest()
        : split(new Split(nullptr))
        , input(this->split)
    {
    }

    MockApplication mockApplication;
    Split *split;
    SplitInput input;
};

}  // namespace

TEST_P(SplitInputTest, Reply)
{
    std::tuple<QString, QString> params = this->GetParam();
    auto [inputText, expected] = params;
    ASSERT_EQ("", this->input.getInputText());
    this->input.setInputText(inputText);
    ASSERT_EQ(inputText, this->input.getInputText());

    auto *message = new Message();
    message->displayName = "forsen";
    auto reply = MessagePtr(message);
    this->input.setReply(reply);
    QString actual = this->input.getInputText();
    ASSERT_EQ(expected, actual) << "Input text after setReply should be '"
                                << expected << "', but got '" << actual << "'";
}

INSTANTIATE_TEST_SUITE_P(
    SplitInput, SplitInputTest,
    testing::Values(
        // Ensure message is retained
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "Test message",
            // Expected text after replying to forsen
            "@forsen Test message "),

        // Ensure mention is stripped, no message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "@forsen",
            // Expected text after replying to forsen
            "@forsen "),

        // Ensure mention with space is stripped, no message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "@forsen ",
            // Expected text after replying to forsen
            "@forsen "),

        // Ensure mention is stripped, retain message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "@forsen Test message",
            // Expected text after replying to forsen
            "@forsen Test message "),

        // Ensure mention with comma is stripped, no message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "@forsen,",
            // Expected text after replying to forsen
            "@forsen "),

        // Ensure mention with comma is stripped, retain message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "@forsen Test message",
            // Expected text after replying to forsen
            "@forsen Test message "),

        // Ensure mention with comma and space is stripped, no message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "@forsen, ",
            // Expected text after replying to forsen
            "@forsen "),

        // Ensure it works with no message
        std::make_tuple<QString, QString>(
            // Pre-existing text in the input
            "",
            // Expected text after replying to forsen
            "@forsen ")));

TEST(SplitInputRestriction, StatusDistinctFromSendWait)
{
    MockApplication app;
    auto *split = new Split(nullptr);
    SplitInput input(split);

    ASSERT_TRUE(input.restrictionStatus().isEmpty());
    input.setShadowConnectionStatus(ShadowConnectionState::Connected);
    ASSERT_EQ(input.restrictionStatus(), QStringLiteral("ShadowChat"));
    ASSERT_EQ(input.shadowConnectionStatus(),
              ShadowConnectionState::Connected);
    input.setSendWaitStatus(QStringLiteral("10s"));
    ASSERT_EQ(input.restrictionStatus(), QStringLiteral("ShadowChat"));
    input.setShadowConnectionStatus(ShadowConnectionState::Disconnected);
    ASSERT_EQ(input.restrictionStatus(), QStringLiteral("ShadowChat"));
    ASSERT_EQ(input.shadowConnectionStatus(),
              ShadowConnectionState::Disconnected);
    input.setShadowConnectionStatus(std::nullopt);
    ASSERT_TRUE(input.restrictionStatus().isEmpty());
    ASSERT_FALSE(input.shadowConnectionStatus().has_value());
}

TEST(SplitInputRestriction, StatusFollowsSendTarget)
{
    MockApplication app;
    auto *split = new Split(nullptr);

    ASSERT_EQ(split->getShadowSendTarget(), ShadowSendTarget::Shadow);
    split->updateRestrictionStatus();
    ASSERT_EQ(split->getInput().restrictionStatus(),
              QStringLiteral("ShadowChat"));
    ASSERT_EQ(split->getInput().shadowConnectionStatus(),
              ShadowConnectionState::Disconnected);

    split->setShadowSendTarget(ShadowSendTarget::Normal);
    ASSERT_TRUE(split->getInput().restrictionStatus().isEmpty());

    split->setShadowSendTarget(ShadowSendTarget::Shadow);
    ASSERT_EQ(split->getInput().restrictionStatus(),
              QStringLiteral("ShadowChat"));
}

TEST(SplitInputRestriction, ConnectionDotStates)
{
    MockApplication app;
    auto *split = new Split(nullptr);
    SplitInput input(split);

    input.setShadowConnectionStatus(ShadowConnectionState::Connecting);
    ASSERT_EQ(input.restrictionStatus(), QStringLiteral("ShadowChat"));
    ASSERT_EQ(input.shadowConnectionStatus(),
              ShadowConnectionState::Connecting);
}
