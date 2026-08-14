// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/TwitchEmotes.hpp"

#include "messages/Emote.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/TwitchUser.hpp"
#include "Test.hpp"

#include <QJsonObject>
#include <QString>

using namespace chatterino;
using namespace Qt::Literals;

namespace {

class TestTwitchEmotes : public ITwitchEmotes
{
public:
    EmotePtr getOrCreateEmote(const EmoteId &id, const EmoteName &name) override
    {
        return std::make_shared<Emote>(Emote{
            .name = name,
            .id = id,
        });
    }
};

std::shared_ptr<TwitchUser> makeOwner()
{
    return std::make_shared<TwitchUser>(TwitchUser{
        .id = u"11148817"_s,
        .name = u"pajlada"_s,
        .displayName = u"pajlada"_s,
    });
}

const TwitchEmoteSet *findSetByKind(const std::vector<TwitchEmoteSet> &sets,
                                    TwitchEmoteSetKind kind)
{
    for (const auto &set : sets)
    {
        if (set.kind == kind)
        {
            return &set;
        }
    }
    return nullptr;
}

}  // namespace

TEST(TwitchEmotes, HelixChannelEmoteParsesTier)
{
    HelixChannelEmote emote(QJsonObject{
        {u"id"_s, u"emote-t2"_s},
        {u"name"_s, u"TierTwoEmote"_s},
        {u"emote_type"_s, u"subscriptions"_s},
        {u"emote_set_id"_s, u"set-t2"_s},
        {u"owner_id"_s, u"11148817"_s},
        {u"tier"_s, u"2000"_s},
    });

    EXPECT_EQ(emote.tier, u"2000"_s);
    auto info = emote.toChannelInfo();
    EXPECT_EQ(info.id, u"emote-t2"_s);
    EXPECT_EQ(info.name, u"TierTwoEmote"_s);
    EXPECT_EQ(info.type, u"subscriptions"_s);
    EXPECT_EQ(info.tier, u"2000"_s);
}

TEST(TwitchEmotes, HelixChannelEmoteMissingTierStaysEmpty)
{
    HelixChannelEmote emote(QJsonObject{
        {u"id"_s, u"emote-f"_s},
        {u"name"_s, u"FollowEmote"_s},
        {u"emote_type"_s, u"follower"_s},
        {u"emote_set_id"_s, u"set-f"_s},
        {u"owner_id"_s, u"11148817"_s},
    });

    EXPECT_TRUE(emote.tier.isEmpty());
}

TEST(TwitchEmotes, AccountSetIdIgnoresHelixTier)
{
    HelixChannelEmote emote(QJsonObject{
        {u"id"_s, u"emote-t2"_s},
        {u"name"_s, u"TierTwoEmote"_s},
        {u"emote_type"_s, u"subscriptions"_s},
        {u"emote_set_id"_s, u"set-t2"_s},
        {u"owner_id"_s, u"11148817"_s},
        {u"tier"_s, u"2000"_s},
    });

    auto meta = getTwitchEmoteSetMeta(emote);
    EXPECT_EQ(meta.setID, u"x-c2-s-11148817"_s);
    EXPECT_FALSE(meta.isBits);
    EXPECT_TRUE(meta.isSubLike);
}

TEST(TwitchEmotes, BuildLocalCatalogGroupsTiers)
{
    auto owner = makeOwner();
    TestTwitchEmotes twitchEmotes;
    std::vector<TwitchChannelEmoteInfo> emotes{
        {.id = u"1"_s,
         .name = u"T1Emote"_s,
         .type = u"subscriptions"_s,
         .tier = u"1000"_s},
        {.id = u"2"_s,
         .name = u"T2Emote"_s,
         .type = u"subscriptions"_s,
         .tier = u"2000"_s},
        {.id = u"3"_s,
         .name = u"T3Emote"_s,
         .type = u"subscriptions"_s,
         .tier = u"3000"_s},
        {.id = u"4"_s,
         .name = u"FollowEmote"_s,
         .type = u"follower"_s,
         .tier = {}},
        {.id = u"5"_s,
         .name = u"BitsEmote"_s,
         .type = u"bitstier"_s,
         .tier = {}},
        {.id = u"6"_s,
         .name = u"EmptyTierSub"_s,
         .type = u"subscriptions"_s,
         .tier = {}},
    };

    auto withFollow =
        buildLocalTwitchEmoteCatalog(emotes, true, owner, twitchEmotes);
    EXPECT_TRUE(withFollow.emotes.contains(EmoteName{u"T1Emote"_s}));
    EXPECT_TRUE(withFollow.emotes.contains(EmoteName{u"T2Emote"_s}));
    EXPECT_TRUE(withFollow.emotes.contains(EmoteName{u"T3Emote"_s}));
    EXPECT_TRUE(withFollow.emotes.contains(EmoteName{u"FollowEmote"_s}));
    EXPECT_TRUE(withFollow.emotes.contains(EmoteName{u"EmptyTierSub"_s}));
    EXPECT_FALSE(withFollow.emotes.contains(EmoteName{u"BitsEmote"_s}));

    ASSERT_EQ(withFollow.sets.size(), 4);
    EXPECT_EQ(withFollow.sets[0].kind, TwitchEmoteSetKind::Tier1);
    EXPECT_EQ(withFollow.sets[1].kind, TwitchEmoteSetKind::Tier2);
    EXPECT_EQ(withFollow.sets[2].kind, TwitchEmoteSetKind::Tier3);
    EXPECT_EQ(withFollow.sets[3].kind, TwitchEmoteSetKind::Follower);
    EXPECT_EQ(withFollow.sets[0].title(), u"pajlada (Tier 1)"_s);
    EXPECT_EQ(withFollow.sets[1].title(), u"pajlada (Tier 2)"_s);
    EXPECT_EQ(withFollow.sets[2].title(), u"pajlada (Tier 3)"_s);
    EXPECT_EQ(withFollow.sets[3].title(), u"pajlada (Follower)"_s);
    EXPECT_TRUE(
        withFollow.sets[0].emotes.contains(EmoteName{u"EmptyTierSub"_s}));

    auto withoutFollow =
        buildLocalTwitchEmoteCatalog(emotes, false, owner, twitchEmotes);
    EXPECT_FALSE(withoutFollow.emotes.contains(EmoteName{u"FollowEmote"_s}));
    EXPECT_TRUE(withoutFollow.emotes.contains(EmoteName{u"T2Emote"_s}));
    EXPECT_EQ(findSetByKind(withoutFollow.sets, TwitchEmoteSetKind::Follower),
              nullptr);
}

TEST(TwitchEmotes, BuildLocalCatalogKeepsFirstName)
{
    auto owner = makeOwner();
    TestTwitchEmotes twitchEmotes;
    std::vector<TwitchChannelEmoteInfo> emotes{
        {.id = u"first"_s,
         .name = u"SameName"_s,
         .type = u"subscriptions"_s,
         .tier = u"1000"_s},
        {.id = u"second"_s,
         .name = u"SameName"_s,
         .type = u"subscriptions"_s,
         .tier = u"2000"_s},
    };

    auto catalog =
        buildLocalTwitchEmoteCatalog(emotes, false, owner, twitchEmotes);
    ASSERT_EQ(catalog.emotes.size(), 1);
    auto it = catalog.emotes.find(EmoteName{u"SameName"_s});
    ASSERT_NE(it, catalog.emotes.end());
    EXPECT_EQ(it->second->id.string, u"first"_s);
}

TEST(TwitchEmotes, BitsTitleUnchanged)
{
    auto owner = makeOwner();
    TwitchEmoteSet bits{
        .owner = owner,
        .emotes = {},
        .isBits = true,
        .isSubLike = true,
    };
    EXPECT_EQ(bits.title(), u"pajlada (Bits)"_s);
}

TEST(TwitchEmotes, SkipAccountSubSetOnlyWhenLocalCatalogExists)
{
    auto owner = makeOwner();
    TwitchEmoteSet sub{
        .owner = owner,
        .emotes = {},
        .isBits = false,
        .isSubLike = true,
    };
    TwitchEmoteSet bits{
        .owner = owner,
        .emotes = {},
        .isBits = true,
        .isSubLike = true,
    };
    TwitchEmoteSet other{
        .owner = std::make_shared<TwitchUser>(TwitchUser{
            .id = u"999"_s,
            .name = u"other"_s,
            .displayName = u"other"_s,
        }),
        .emotes = {},
        .isBits = false,
        .isSubLike = true,
    };

    EXPECT_FALSE(skipCurrentChannelAccountSubSet(false, u"11148817"_s, sub));
    EXPECT_TRUE(skipCurrentChannelAccountSubSet(true, u"11148817"_s, sub));
    EXPECT_FALSE(skipCurrentChannelAccountSubSet(true, u"11148817"_s, bits));
    EXPECT_FALSE(skipCurrentChannelAccountSubSet(true, u"11148817"_s, other));
}
