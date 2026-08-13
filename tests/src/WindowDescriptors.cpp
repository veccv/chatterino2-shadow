// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/WindowDescriptors.hpp"

#include "Test.hpp"

#include <QJsonObject>

using namespace chatterino;

TEST(WindowDescriptors, ShadowViewModeRoundTrip)
{
    for (auto mode : {ShadowViewMode::Both, ShadowViewMode::Normal,
                      ShadowViewMode::Shadow})
    {
        SplitDescriptor descriptor;
        descriptor.type_ = QStringLiteral("Twitch");
        descriptor.channelName_ = QStringLiteral("pajlada");
        descriptor.shadowViewMode_ = mode;

        auto loaded = SplitDescriptor::loadFromJSON(descriptor.toJson());
        ASSERT_EQ(loaded.shadowViewMode_, mode);
    }
}

TEST(WindowDescriptors, MissingShadowViewModeLoadsAsBoth)
{
    QJsonObject root;
    root.insert(QStringLiteral("type"), QStringLiteral("split"));
    root.insert(QStringLiteral("data"),
                QJsonObject{{QStringLiteral("type"), QStringLiteral("Twitch")},
                            {QStringLiteral("name"), QStringLiteral("pajlada")}});

    auto loaded = SplitDescriptor::loadFromJSON(root);
    ASSERT_EQ(loaded.shadowViewMode_, ShadowViewMode::Both);
}
