// ps5-homebrew-ui - Tests: ORION's saved preferences and look.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/save_file.hpp"
#include "library/json.hpp"
#include "orion/look.hpp"
#include "orion/prefs.hpp"
#include "orion/theme_file.hpp"

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

TEST(OrionPrefs, SystemsKeepTheirSwitchAndCore)
{
    const std::string dir = fresh_dir("orion-prefs-systems");
    orion::LibraryPrefs prefs;
    EXPECT_TRUE(prefs.system_enabled("Nintendo - Game Boy Advance")); // on until turned off
    prefs.set_system_enabled("Nintendo - Game Boy Advance", false);
    prefs.set_system_core("Nintendo - Nintendo DS", "desmume_libretro.so");
    ASSERT_TRUE(orion::save_prefs(dir, prefs));
    const orion::LibraryPrefs back = orion::load_prefs(dir);
    EXPECT_FALSE(back.system_enabled("Nintendo - Game Boy Advance"));
    EXPECT_TRUE(back.system_enabled("Nintendo - Nintendo DS"));
    EXPECT_EQ(back.system_core("Nintendo - Nintendo DS"), "desmume_libretro.so");
    EXPECT_EQ(back.system_core("Sega - Saturn"), "");
    // A core is a file name, never a path.
    ASSERT_TRUE(save::write_atomic(dir + "/library.json",
                                   R"({"systems": {"X": {"core": "../../evil.so"}}})")
                    .empty());
    EXPECT_EQ(orion::load_prefs(dir).system_core("X"), "");
}

TEST(OrionPrefs, TabsKeepTheirOwnThemes)
{
    const std::string dir = fresh_dir("orion-prefs-tab-themes");
    orion::LibraryPrefs prefs;
    EXPECT_EQ(prefs.tab_theme("games"), ""); // the main look until chosen
    prefs.set_tab_theme("games", "candy");
    prefs.set_tab_theme("sys:Nintendo - Game Boy Advance", "sunset-nebula");
    prefs.set_tab_theme("games", "paper"); // a second choice replaces the first
    ASSERT_TRUE(orion::save_prefs(dir, prefs));
    orion::LibraryPrefs back = orion::load_prefs(dir);
    EXPECT_EQ(back.tab_theme("games"), "paper");
    EXPECT_EQ(back.tab_theme("sys:Nintendo - Game Boy Advance"), "sunset-nebula");
    EXPECT_EQ(back.tab_theme("all"), "");
    back.set_tab_theme("games", ""); // back to the main look
    EXPECT_EQ(back.tab_theme("games"), "");
    EXPECT_EQ(back.tab_prefs.size(), 1u);
}

TEST(OrionPrefs, EachTabKeepsItsCoversSortAndPlace)
{
    const std::string dir = fresh_dir("orion-prefs-tab-settings");
    orion::LibraryPrefs prefs;
    EXPECT_EQ(prefs.tab_cover("games"), orion::CoverShape::automatic);
    prefs.set_tab_cover("sys:Sony - PlayStation 2", orion::CoverShape::portrait);
    prefs.set_tab_sort("sys:Sony - PlayStation 2", "recent");
    prefs.set_tab_hidden("media", true);
    prefs.set_tab_hidden("all", true); // All always shows
    ASSERT_TRUE(orion::save_prefs(dir, prefs));
    orion::LibraryPrefs back = orion::load_prefs(dir);
    EXPECT_EQ(back.tab_cover("sys:Sony - PlayStation 2"), orion::CoverShape::portrait);
    EXPECT_EQ(back.tab_sort("sys:Sony - PlayStation 2"), "recent");
    EXPECT_EQ(back.tab_sort("games"), "");
    EXPECT_TRUE(back.tab_hidden("media"));
    EXPECT_FALSE(back.tab_hidden("all"));
    // Back to every default: the entry goes.
    back.set_tab_hidden("media", false);
    back.set_tab_cover("sys:Sony - PlayStation 2", orion::CoverShape::automatic);
    back.set_tab_sort("sys:Sony - PlayStation 2", "");
    EXPECT_TRUE(back.tab_prefs.empty());

    // A file from before tab_settings: its themes still apply.
    ASSERT_TRUE(
        save::write_atomic(dir + "/library.json", R"({"tab_themes": {"games": "candy"}})").empty());
    EXPECT_EQ(orion::load_prefs(dir).tab_theme("games"), "candy");
}

TEST(OrionPrefs, BoxArtLivesWhereThePlayerSays)
{
    const std::string dir = fresh_dir("orion-prefs-art-dir");
    orion::LibraryPrefs prefs;
    EXPECT_EQ(orion::art_folder(prefs, "/data/orion"), "/data/orion/art");
    prefs.art_dir = "/mnt/usb0/ORION/art";
    ASSERT_TRUE(orion::save_prefs(dir, prefs));
    EXPECT_EQ(orion::art_folder(orion::load_prefs(dir), "/data/orion"), "/mnt/usb0/ORION/art");
    EXPECT_TRUE(orion::valid_art_dir("/data/my art"));
    EXPECT_FALSE(orion::valid_art_dir("/system/art"));     // not under /data or /mnt
    EXPECT_FALSE(orion::valid_art_dir("/data/../system")); // no way out
    EXPECT_FALSE(orion::valid_art_dir("/data/art/"));
    EXPECT_FALSE(orion::valid_art_dir("art"));
    // A hand-edited file with a bad folder falls back to ORION's own.
    ASSERT_TRUE(save::write_atomic(dir + "/library.json", R"({"art_dir": "/etc/x"})").empty());
    EXPECT_EQ(orion::load_prefs(dir).art_dir, "");
}

TEST(OrionPrefs, GamesBringOrionBackUnlessToldNot)
{
    const std::string dir = fresh_dir("orion-prefs-return");
    orion::LibraryPrefs prefs;
    EXPECT_TRUE(prefs.return_on_close);
    EXPECT_FALSE(prefs.shortcut_test);
    prefs.return_on_close = false;
    prefs.shortcut_test = true;
    ASSERT_TRUE(orion::save_prefs(dir, prefs));
    const orion::LibraryPrefs back = orion::load_prefs(dir);
    EXPECT_FALSE(back.return_on_close);
    EXPECT_TRUE(back.shortcut_test);
}

TEST(OrionPrefs, RomFoldersAreKept)
{
    const std::string dir = fresh_dir("orion-prefs-rom-folders");
    orion::LibraryPrefs prefs;
    prefs.rom_folders.push_back({"/mnt/usb0/roms", ""});
    prefs.rom_folders.push_back({"/data/games/psp", "Sony - PlayStation Portable"});
    prefs.rom_folders.push_back({"/etc/nope", ""}); // not under /data or /mnt: dropped on load
    ASSERT_TRUE(orion::save_prefs(dir, prefs));
    const orion::LibraryPrefs back = orion::load_prefs(dir);
    ASSERT_EQ(back.rom_folders.size(), 2u);
    EXPECT_EQ(back.rom_folders[0].path, "/mnt/usb0/roms");
    EXPECT_EQ(back.rom_folders[1].system, "Sony - PlayStation Portable");
}

TEST(OrionPrefs, BackgroundsCanBeToldApart)
{
    const std::string dir = fresh_dir("orion-prefs-backgrounds");
    orion::LibraryPrefs prefs;
    EXPECT_TRUE(prefs.game_backgrounds);
    EXPECT_TRUE(prefs.blurred_covers);
    EXPECT_FLOAT_EQ(prefs.background_strength, 1.0f);
    prefs.blurred_covers = false;
    prefs.background_strength = 0.5f;
    ASSERT_TRUE(orion::save_prefs(dir, prefs));
    orion::LibraryPrefs back = orion::load_prefs(dir);
    EXPECT_FALSE(back.blurred_covers);
    EXPECT_NEAR(back.background_strength, 0.5f, 0.01f);
    // Out of range by hand: kept within it.
    ASSERT_TRUE(save::write_atomic(dir + "/library.json", R"({"background_strength": 7})").empty());
    EXPECT_FLOAT_EQ(orion::load_prefs(dir).background_strength, 1.0f);
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

// The kit's themes, as a library: what ORION falls back to with no files.
const orion::ThemeLibrary &kit()
{
    static const orion::ThemeLibrary themes = []
    {
        orion::ThemeLibrary library;
        library.load("", "");
        return library;
    }();
    return themes;
}

TEST(OrionLook, DefaultIsTheOrionTheme)
{
    const orion::Look look = orion::default_look(kit());
    EXPECT_EQ(look.base, "orion");
    EXPECT_EQ(look.background, gfx::BackdropMode::stars);
    const ui::Theme theme = orion::themed(look, kit());
    EXPECT_STREQ(theme.id, "orion");
    EXPECT_TRUE(orion::same_rgb(theme.accent, kit().find("orion")->theme.accent));
    // An unknown base falls back to ORION.
    orion::Look lost = look;
    lost.base = "no such theme";
    EXPECT_STREQ(orion::themed(lost, kit()).id, "orion");
}

TEST(OrionLook, ChoicesSurviveASaveAndShortFilesKeepTheBase)
{
    const std::string dir = fresh_dir("orion-look");
    orion::Look look = orion::default_look(kit());
    look.background = gfx::BackdropMode::aurora;
    look.accent = gfx::Color::rgb(0xffc94a);
    look.glass = gfx::Color::rgb(0xffffff, 0.25f);
    look.clouds = 1.5f;
    look.motion = false;
    look.corners = 30.0f;
    ASSERT_TRUE(orion::save_look(dir, look));

    const orion::Look back = orion::load_look(dir, kit());
    EXPECT_EQ(back.background, gfx::BackdropMode::aurora);
    EXPECT_TRUE(orion::same_rgb(back.accent, gfx::Color::rgb(0xffc94a)));
    EXPECT_NEAR(back.glass.a, 0.25f, 0.01f);
    EXPECT_NEAR(back.clouds, 1.5f, 0.01f);
    EXPECT_FALSE(back.motion);
    EXPECT_FLOAT_EQ(back.corners, 30.0f);

    // A file that names only a base takes everything else from that theme.
    ASSERT_TRUE(save::write_atomic(dir + "/theme.json", R"({"base": "acrylic"})").empty());
    const orion::Look acrylic = orion::load_look(dir, kit());
    EXPECT_EQ(acrylic.base, "acrylic");
    EXPECT_TRUE(orion::same_rgb(acrylic.accent, kit().find("acrylic")->theme.accent));
    // Junk values are clamped or ignored.
    ASSERT_TRUE(save::write_atomic(dir + "/theme.json",
                                   R"({"accent": "pink", "clouds": 900, "corners": -5})")
                    .empty());
    const orion::Look junk = orion::load_look(dir, kit());
    EXPECT_TRUE(orion::same_rgb(junk.accent, orion::default_look(kit()).accent));
    EXPECT_FLOAT_EQ(junk.clouds, 2.0f);
    EXPECT_FLOAT_EQ(junk.corners, 0.0f);
}

TEST(OrionThemeFile, EveryKitThemeSurvivesAWriteAndARead)
{
    for (const ui::Theme &theme : ui::themes())
    {
        const orion::ThemeFile file = orion::from_kit(theme);
        orion::ThemeFile back;
        std::string error;
        ASSERT_TRUE(orion::parse_theme(orion::write_theme(file), nullptr, &back, &error))
            << theme.id << ": " << error;
        EXPECT_EQ(back.id, file.id);
        EXPECT_STREQ(back.theme.id, theme.id);
        EXPECT_EQ(back.theme.style, theme.style) << theme.id;
        EXPECT_EQ(back.theme.corner, theme.corner) << theme.id;
        EXPECT_EQ(back.theme.backdrop.mode, theme.backdrop.mode) << theme.id;
        EXPECT_FLOAT_EQ(back.theme.radius, theme.radius) << theme.id;
        EXPECT_EQ(back.theme.heading, theme.heading) << theme.id;
        EXPECT_EQ(back.theme.sounds, theme.sounds) << theme.id;
        EXPECT_TRUE(orion::same_rgb(back.theme.accent, theme.accent)) << theme.id;
        EXPECT_NEAR(back.theme.surface.a, theme.surface.a, 0.01f) << theme.id;
        EXPECT_TRUE(orion::same_rgb(back.theme.backdrop.colors[2], theme.backdrop.colors[2]))
            << theme.id;
    }
}

TEST(OrionThemeFile, AShortFileExtendsAnotherTheme)
{
    const char *text = R"({
        "format": "orion-theme", "version": 1,
        "id": "nebula-noir", "name": "Nebula Noir", "author": "test",
        "extends": "acrylic",
        "page": {"background": "galaxy", "colors": ["#000000", "#010203"]},
        "palette": {"accent": "#ffc94a80"},
        "shape": {"radius": 999, "style": "no such style"},
        "orion": {"clouds": 50, "cover_corners": 30}
    })";
    orion::ThemeFile file;
    std::string error;
    ASSERT_TRUE(orion::parse_theme(text, nullptr, &file, &error)) << error;
    const ui::Theme &acrylic = kit().find("acrylic")->theme;
    EXPECT_STREQ(file.theme.id, "nebula-noir");
    EXPECT_EQ(file.author, "test");
    EXPECT_EQ(file.theme.backdrop.mode, gfx::BackdropMode::stars);
    EXPECT_TRUE(orion::same_rgb(file.theme.backdrop.colors[1], gfx::Color::rgb(0x010203)));
    // What the file leaves out is Acrylic's.
    EXPECT_TRUE(orion::same_rgb(file.theme.backdrop.colors[2], acrylic.backdrop.colors[2]));
    EXPECT_EQ(file.theme.style, acrylic.style);
    EXPECT_TRUE(orion::same_rgb(file.theme.accent, gfx::Color::rgb(0xffc94a)));
    EXPECT_NEAR(file.theme.accent.a, 128.0f / 255.0f, 0.01f);
    EXPECT_FLOAT_EQ(file.theme.radius, 200.0f); // clamped
    EXPECT_FLOAT_EQ(file.clouds, 0.5f);
    EXPECT_FLOAT_EQ(file.cover_corners, 30.0f);
}

TEST(OrionThemeFile, RefusesWhatIsNotATheme)
{
    const char *broken[] = {
        "{",
        R"([1, 2])",
        R"({"format": "something-else", "id": "a", "name": "A"})",
        R"({"version": 9, "id": "a", "name": "A"})",
        R"({"id": "Has Spaces", "name": "A"})",
        R"({"id": "ok"})",
    };
    for (const char *text : broken)
    {
        orion::ThemeFile file;
        std::string error;
        EXPECT_FALSE(orion::parse_theme(text, nullptr, &file, &error)) << text;
        EXPECT_FALSE(error.empty()) << text;
    }
    EXPECT_EQ(orion::theme_id_from_name("Nebula Noir!"), "nebula-noir");
    EXPECT_EQ(orion::theme_id_from_name("  --  "), "");
}

TEST(OrionThemeFile, TheLibraryReadsBothFoldersAndSavesThePlayers)
{
    const std::string root = ::testing::TempDir() + "orion-themes";
    const std::string builtin = root + "/builtin";
    const std::string user = root + "/user";
    for (const std::string &dir : {root, builtin, user})
        ASSERT_TRUE(save::ensure_directory(dir));
    for (const char *name : {"/zeta.json", "/orion.json"})
        std::remove((builtin + name).c_str());
    for (const char *name : {"/mine.json", "/broken.json", "/clash.json", "/saved.json"})
        std::remove((user + name).c_str());

    ASSERT_TRUE(save::write_atomic(builtin + "/zeta.json",
                                   R"({"id": "zeta", "name": "Zeta", "extends": "brutal"})")
                    .empty());
    ASSERT_TRUE(save::write_atomic(builtin + "/orion.json",
                                   orion::write_theme(orion::from_kit(kit().find("orion")->theme)))
                    .empty());
    ASSERT_TRUE(save::write_atomic(user + "/mine.json",
                                   R"({"id": "mine", "name": "Mine", "extends": "zeta"})")
                    .empty());
    ASSERT_TRUE(save::write_atomic(user + "/broken.json", "{nope").empty());
    ASSERT_TRUE(
        save::write_atomic(user + "/clash.json", R"({"id": "zeta", "name": "Again"})").empty());

    orion::ThemeLibrary themes;
    themes.load(builtin, user);
    ASSERT_EQ(themes.size(), 3u);
    EXPECT_EQ(themes.at(0).id, "orion"); // ORION leads
    EXPECT_FALSE(themes.at(0).user);
    const orion::ThemeFile *mine = themes.find("mine");
    ASSERT_NE(mine, nullptr);
    EXPECT_TRUE(mine->user);
    // "mine" extends "zeta", which extends Brutal.
    EXPECT_EQ(mine->theme.style, kit().find("brutal")->theme.style);
    EXPECT_EQ(themes.problems().size(), 2u); // broken.json and the clashing id

    orion::ThemeFile saved = *mine;
    saved.id = "saved";
    saved.name = "Saved";
    std::string error;
    ASSERT_TRUE(themes.save_user_theme(saved, &error)) << error;
    EXPECT_NE(themes.find("saved"), nullptr);
    orion::ThemeFile builtin_clash = saved;
    builtin_clash.id = "orion";
    EXPECT_FALSE(themes.save_user_theme(builtin_clash, &error));

    // Read again from disk: the saved theme is there.
    orion::ThemeLibrary again;
    again.load(builtin, user);
    EXPECT_NE(again.find("saved"), nullptr);
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
