// ps5-homebrew-ui - Tests: ORION's saved preferences and look.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "library/json.hpp"
#include "orion/look.hpp"
#include "orion/prefs.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

namespace hui
{
namespace
{

std::string fresh_dir(const char *name)
{
    const std::string dir = ::testing::TempDir() + name;
    EXPECT_TRUE(save::ensure_directory(dir));
    std::remove((dir + "/library.json").c_str());
    std::remove((dir + "/theme.json").c_str());
    return dir;
}

TEST(Json, QuotesWhatAWriterNeeds)
{
    EXPECT_EQ(json::quote("plain"), "\"plain\"");
    EXPECT_EQ(json::quote("a\"b\\c\nd\x01"), "\"a\\\"b\\\\c\\nd\\u0001\"");
    json::Value back;
    ASSERT_TRUE(json::parse(json::quote("RPGs \xe2\x98\x85 \"best\""), &back));
    EXPECT_EQ(back.string_or(""), "RPGs \xe2\x98\x85 \"best\"");
}

TEST(OrionPrefs, TabsAndPlacementsRoundTrip)
{
    const std::string dir = fresh_dir("orion-prefs");
    orion::LibraryPrefs prefs = orion::load_prefs(dir);
    EXPECT_TRUE(prefs.tabs.empty());
    EXPECT_EQ(prefs.last_tab, "all");
    EXPECT_TRUE(prefs.reopen_last_tab);

    const std::string rpgs = prefs.add_tab("  RPGs \"2026\"  ").id;
    const std::string kids = prefs.add_tab("").id;
    EXPECT_NE(rpgs, kids);
    EXPECT_EQ(prefs.tab(kids)->name, "New tab");
    EXPECT_TRUE(prefs.place("PPSA01234", rpgs));
    EXPECT_TRUE(prefs.place("CUSA00001", kids));
    EXPECT_FALSE(prefs.place("PPSA01234", "nope"));
    EXPECT_FALSE(prefs.place("not an id", rpgs));
    // Moving a title takes it out of the tab it was in.
    EXPECT_TRUE(prefs.place("CUSA00001", rpgs));
    EXPECT_EQ(prefs.placement("CUSA00001")->id, rpgs);
    prefs.last_tab = "tab:" + rpgs;
    prefs.sort = "recent";
    prefs.moved_leave_shelf = false;
    ASSERT_TRUE(orion::save_prefs(dir, prefs));

    const orion::LibraryPrefs back = orion::load_prefs(dir);
    ASSERT_EQ(back.tabs.size(), 2u);
    EXPECT_EQ(back.tabs[0].name, "RPGs \"2026\"");
    EXPECT_EQ(back.placement("PPSA01234")->id, rpgs);
    EXPECT_EQ(back.placement("CUSA00001")->id, rpgs);
    EXPECT_EQ(back.last_tab, "tab:" + rpgs);
    EXPECT_EQ(back.sort, "recent");
    EXPECT_FALSE(back.moved_leave_shelf);
}

TEST(OrionPrefs, RemovingATabSendsItsTitlesHome)
{
    orion::LibraryPrefs prefs;
    const std::string a = prefs.add_tab("A").id;
    const std::string b = prefs.add_tab("B").id;
    prefs.place("PPSA01234", a);
    prefs.last_tab = "tab:" + a;
    EXPECT_TRUE(prefs.move_tab(b, -1));
    EXPECT_EQ(prefs.tabs[0].id, b);
    EXPECT_FALSE(prefs.move_tab(b, -1)); // already first
    EXPECT_TRUE(prefs.rename_tab(a, "Shooters"));
    EXPECT_TRUE(prefs.remove_tab(a));
    EXPECT_EQ(prefs.placement("PPSA01234"), nullptr);
    EXPECT_EQ(prefs.last_tab, "all");
    EXPECT_FALSE(prefs.remove_tab(a));
}

TEST(OrionPrefs, BrokenOrHandEditedFilesAreForgiven)
{
    const std::string dir = fresh_dir("orion-prefs-edited");
    ASSERT_TRUE(save::write_atomic(dir + "/library.json",
                                   R"({"tabs": [{"id": "x1", "name": "Kept"}, {"id": "bad id!"},
                                        {"id": "x1", "name": "Duplicate"}],
                                       "placements": {"PPSA01234": "x1", "PPSA00002": "gone"},
                                       "extra": [1, 2, 3]})")
                    .empty());
    const orion::LibraryPrefs prefs = orion::load_prefs(dir);
    ASSERT_EQ(prefs.tabs.size(), 1u);
    EXPECT_EQ(prefs.tabs[0].name, "Kept");
    EXPECT_NE(prefs.placement("PPSA01234"), nullptr);
    EXPECT_EQ(prefs.placement("PPSA00002"), nullptr);

    ASSERT_TRUE(save::write_atomic(dir + "/library.json", "{nope").empty());
    EXPECT_TRUE(orion::load_prefs(dir).tabs.empty());
    EXPECT_FALSE(orion::save_prefs("", prefs));
}

TEST(OrionLook, DefaultIsTheOrionTheme)
{
    const orion::Look look = orion::default_look();
    EXPECT_EQ(look.base, "orion");
    EXPECT_EQ(look.background, gfx::BackdropMode::stars);
    const ui::Theme theme = orion::themed(look);
    EXPECT_STREQ(theme.id, "orion");
    EXPECT_TRUE(orion::same_rgb(theme.accent, orion::base_theme("orion").accent));
    // An unknown base falls back to ORION.
    EXPECT_STREQ(orion::base_theme("no such theme").id, "orion");
}

TEST(OrionLook, ChoicesSurviveASaveAndShortFilesKeepTheBase)
{
    const std::string dir = fresh_dir("orion-look");
    orion::Look look = orion::default_look();
    look.background = gfx::BackdropMode::aurora;
    look.accent = gfx::Color::rgb(0xffc94a);
    look.glass = gfx::Color::rgb(0xffffff, 0.25f);
    look.clouds = 1.5f;
    look.motion = false;
    look.corners = 30.0f;
    ASSERT_TRUE(orion::save_look(dir, look));

    const orion::Look back = orion::load_look(dir);
    EXPECT_EQ(back.background, gfx::BackdropMode::aurora);
    EXPECT_TRUE(orion::same_rgb(back.accent, gfx::Color::rgb(0xffc94a)));
    EXPECT_NEAR(back.glass.a, 0.25f, 0.01f);
    EXPECT_NEAR(back.clouds, 1.5f, 0.01f);
    EXPECT_FALSE(back.motion);
    EXPECT_FLOAT_EQ(back.corners, 30.0f);

    // A file that names only a base takes everything else from that theme.
    ASSERT_TRUE(save::write_atomic(dir + "/theme.json", R"({"base": "acrylic"})").empty());
    const orion::Look acrylic = orion::load_look(dir);
    EXPECT_EQ(acrylic.base, "acrylic");
    EXPECT_TRUE(orion::same_rgb(acrylic.accent, orion::base_theme("acrylic").accent));
    // Junk values are clamped or ignored.
    ASSERT_TRUE(save::write_atomic(dir + "/theme.json",
                                   R"({"accent": "pink", "clouds": 900, "corners": -5})")
                    .empty());
    const orion::Look junk = orion::load_look(dir);
    EXPECT_TRUE(orion::same_rgb(junk.accent, orion::default_look().accent));
    EXPECT_FLOAT_EQ(junk.clouds, 2.0f);
    EXPECT_FLOAT_EQ(junk.corners, 0.0f);
}

TEST(OrionLook, HexRoundTrips)
{
    gfx::Color colour;
    ASSERT_TRUE(orion::from_hex("#1aa6d9", &colour));
    EXPECT_EQ(orion::to_hex(colour), "#1aa6d9");
    ASSERT_TRUE(orion::from_hex("FFC94A", &colour));
    EXPECT_EQ(orion::to_hex(colour), "#ffc94a");
    EXPECT_FALSE(orion::from_hex("#12345", &colour));
    EXPECT_FALSE(orion::from_hex("#12345g", &colour));
    for (const orion::Background &kind : orion::backgrounds())
        EXPECT_STREQ(orion::background_id(kind.mode), kind.id);
}

} // namespace
} // namespace hui
