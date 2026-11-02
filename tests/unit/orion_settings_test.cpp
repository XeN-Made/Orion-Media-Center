// ORION Media Center - Tests: ORION's Settings panel, through the controller.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "component_fixture.hpp"
#include "orion/look.hpp"
#include "orion/settings_panel.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace
{

using hui::Action;
using hui::Direction;
using hui::orion::SettingsPanel;

class OrionSettings : public hui::testing::ComponentFixture
{
  protected:
    SettingsPanel::Change send_to(SettingsPanel &panel, const hui::InputFrame &input)
    {
        feedback_.clear();
        const SettingsPanel::Change change = panel.handle(input, feedback_, &look_, &prefs_);
        for (int i = 0; i < 4; ++i)
            panel.update(kFrame);
        return change;
    }

    hui::orion::Look look_;
    hui::orion::LibraryPrefs prefs_;
};

// Add a game folder opens a browser: the places, then folders, Circle up one
// level, and Use this folder hands the folder back with the emulator it is for.
TEST_F(OrionSettings, TheFolderBrowserPicksAGameFolder)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "hui-orion-browser";
    fs::remove_all(root);
    fs::create_directories(root / "alpha");
    fs::create_directories(root / "beta");
    fs::create_directories(root / ".hidden");

    hui::library::Emulator emulator;
    hui::library::EmulatorApp porpoise;
    porpoise.key = "porpoise";
    porpoise.name = "Porpoise";
    porpoise.title_id = "PPSA90130";
    emulator.apps.push_back(porpoise);

    SettingsPanel panel;
    panel.set_emulator(emulator);
    panel.set_browse_places({{"Nowhere", (root / "missing").string()}, {"Test", root.string()}});
    panel.open(look_, prefs_, feedback_);
    // Library, Tabs, Labels, Emulators; into its rows: Scan, then Porpoise.
    send_to(panel, press(Action::page_next));
    send_to(panel, press(Action::page_next));
    send_to(panel, press(Action::page_next));
    ASSERT_EQ(panel.section(), SettingsPanel::Section::emulators);
    send_to(panel, nav(Direction::right));
    send_to(panel, nav(Direction::down));
    send_to(panel, press(Action::confirm));
    EXPECT_EQ(panel.emulator_page(), "porpoise");
    // Its page: App, Games, Add a game folder.
    send_to(panel, nav(Direction::down));
    send_to(panel, nav(Direction::down));
    EXPECT_EQ(send_to(panel, press(Action::confirm)), SettingsPanel::Change::none);
    // The places that exist (Test), then Type a path: open Test.
    send_to(panel, press(Action::confirm));
    // Use this folder, Up one folder, alpha, beta (not .hidden), Type a path.
    send_to(panel, nav(Direction::down));
    send_to(panel, nav(Direction::down));
    send_to(panel, press(Action::confirm)); // into alpha
    send_to(panel, press(Action::back));    // up again, on alpha
    send_to(panel, press(Action::confirm)); // into alpha once more
    EXPECT_EQ(send_to(panel, press(Action::confirm)), SettingsPanel::Change::add_folder);
    EXPECT_EQ(panel.browse_result(), (root / "alpha").string());
    EXPECT_EQ(panel.emulator_page(), "porpoise");
    auto c = canvas();
    panel.draw(c);
    expect_drawn("the emulator page after the browser");
    fs::remove_all(root);
}

// The rows that change the look preview: Appearance's, and a tab's own page,
// which also says which tab the library should show. Choosing the section
// is not previewing; leaving its rows ends it.
TEST_F(OrionSettings, PreviewsTheLookBesideTheLibrary)
{
    SettingsPanel panel;
    panel.set_tabs({{"all", "All"}, {"games", "Games"}});
    panel.open(look_, prefs_, feedback_);
    EXPECT_FALSE(panel.previewing());
    for (int i = 0; i < 12 && panel.section() != SettingsPanel::Section::appearance; ++i)
        send_to(panel, press(Action::page_next));
    ASSERT_EQ(panel.section(), SettingsPanel::Section::appearance);
    EXPECT_FALSE(panel.previewing()); // still on the sections
    send_to(panel, press(Action::confirm));
    EXPECT_TRUE(panel.previewing());
    EXPECT_EQ(panel.preview_tab(), "");
    for (int i = 0; i < 40; ++i)
        panel.update(kFrame);
    auto c = canvas();
    panel.draw(c);
    expect_drawn("Appearance as a panel beside the library");
    send_to(panel, press(Action::back));
    EXPECT_FALSE(panel.previewing());

    panel.open_tab("games", look_, prefs_, feedback_);
    EXPECT_TRUE(panel.previewing());
    EXPECT_EQ(panel.preview_tab(), "games");
    panel.close(feedback_);
    EXPECT_FALSE(panel.previewing());
    EXPECT_EQ(panel.preview_tab(), "");
}

// Appearance's first row is the theme of the tab behind Settings alone: it
// goes in the prefs for that tab, and the look every tab wears is untouched.
TEST_F(OrionSettings, AppearanceSetsTheThemeOfTheTabBehindIt)
{
    hui::orion::ThemeLibrary themes;
    themes.load(std::string(HUI_SOURCE_DIR) + "/assets/themes", "");
    ASSERT_GT(themes.size(), 2u);
    look_ = hui::orion::default_look(themes);
    const std::string base = look_.base;

    SettingsPanel panel;
    panel.set_themes(themes);
    panel.set_tabs({{"all", "All"}, {"games", "Games"}});
    panel.set_current_tab("games");
    panel.open(look_, prefs_, feedback_);
    for (int i = 0; i < 12 && panel.section() != SettingsPanel::Section::appearance; ++i)
        send_to(panel, press(Action::page_next));
    ASSERT_EQ(panel.section(), SettingsPanel::Section::appearance);
    send_to(panel, press(Action::confirm)); // into the rows: Theme for Games
    EXPECT_EQ(send_to(panel, nav(Direction::right)), SettingsPanel::Change::prefs);
    EXPECT_FALSE(prefs_.tab_theme("games").empty());
    EXPECT_TRUE(prefs_.tab_theme("all").empty());
    EXPECT_EQ(look_.base, base);
    // Back to the theme every tab wears.
    EXPECT_EQ(send_to(panel, nav(Direction::left)), SettingsPanel::Change::prefs);
    EXPECT_TRUE(prefs_.tab_theme("games").empty());
}

// Labels: New label asks for a name; a label's page changes its colour (the
// first choice of a built-in one is ORION's own) and asks for its text.
TEST_F(OrionSettings, ALabelsPageChangesItsColours)
{
    SettingsPanel panel;
    hui::orion::LabelEntry gba;
    gba.id = "gba";
    gba.name = "GBA";
    gba.text = "GBA";
    gba.fill = 0xcdb6ff;
    gba.ink = 0x08101c;
    panel.set_labels({gba});
    panel.open(look_, prefs_, feedback_);
    send_to(panel, press(Action::page_next));
    send_to(panel, press(Action::page_next));
    ASSERT_EQ(panel.section(), SettingsPanel::Section::labels);
    send_to(panel, nav(Direction::right)); // New label
    EXPECT_EQ(send_to(panel, press(Action::confirm)), SettingsPanel::Change::new_label);
    send_to(panel, nav(Direction::down)); // GBA
    send_to(panel, press(Action::confirm));
    EXPECT_EQ(panel.label_id(), "gba");
    // The page opens on Text (the preview above it is not a row to stop on),
    // then Label colour: one step right, from ORION's to the first colour.
    EXPECT_EQ(send_to(panel, press(Action::confirm)), SettingsPanel::Change::rename_label);
    send_to(panel, nav(Direction::down));
    EXPECT_EQ(send_to(panel, nav(Direction::right)), SettingsPanel::Change::prefs);
    ASSERT_NE(prefs_.label("gba"), nullptr);
    EXPECT_EQ(prefs_.label("gba")->fill, hui::orion::label_colours()[0].first);
    EXPECT_EQ(send_to(panel, nav(Direction::left)), SettingsPanel::Change::prefs);
    EXPECT_EQ(prefs_.label("gba"), nullptr); // ORION's again
    send_to(panel, press(Action::back));
    EXPECT_TRUE(panel.label_id().empty());
}

// Backing out of the picture browser that Background picture opened lands
// on that row again, ready to choose once more.
TEST_F(OrionSettings, ReopensOnTheBackgroundPictureRow)
{
    SettingsPanel panel;
    panel.open_at_background(look_, prefs_, feedback_);
    EXPECT_TRUE(panel.is_open());
    EXPECT_EQ(panel.section(), SettingsPanel::Section::appearance);
    EXPECT_EQ(send_to(panel, press(Action::confirm)), SettingsPanel::Change::choose_background);
}

// Appearance, near its end: Background picture asks the screen for a
// picture; with one set, Use the sky takes it away and an effect can fall
// over it.
TEST_F(OrionSettings, ABackgroundPictureIsChosenAndTakenAway)
{
    for (const bool own : {false, true})
    {
        SettingsPanel panel;
        panel.set_background(own);
        panel.open(look_, prefs_, feedback_);
        for (int i = 0; i < 16 && panel.section() != SettingsPanel::Section::appearance; ++i)
            send_to(panel, press(Action::page_next));
        ASSERT_EQ(panel.section(), SettingsPanel::Section::appearance);
        send_to(panel, nav(Direction::right));
        // To Restore the ORION look, the last row, then back up past Save as
        // new theme.
        for (int i = 0; i < 60; ++i)
            send_to(panel, nav(Direction::down));
        send_to(panel, nav(Direction::up));
        send_to(panel, nav(Direction::up));
        if (own)
        {
            EXPECT_EQ(send_to(panel, press(Action::confirm)),
                      SettingsPanel::Change::clear_background);
            // Effect over your picture, just above: right picks the next one.
            send_to(panel, nav(Direction::up));
            EXPECT_EQ(prefs_.picture_effect, "none");
            EXPECT_EQ(send_to(panel, nav(Direction::right)), SettingsPanel::Change::prefs);
            EXPECT_EQ(prefs_.picture_effect, "snow");
            send_to(panel, nav(Direction::up));
        }
        EXPECT_EQ(send_to(panel, press(Action::confirm)), SettingsPanel::Change::choose_background);
    }
}

// Network drives: while a game goes in, Stop installing comes first; a
// drive's page offers Install a PS5 game, just above Remove.
TEST_F(OrionSettings, ANetworkDriveInstallsAGameAndStopsIt)
{
    SettingsPanel panel;
    hui::orion::Share share;
    share.id = "s1";
    share.server = "nas";
    share.share = "games";
    panel.set_shares({share}, "");
    panel.set_installing("Moon Garden");
    panel.open(look_, prefs_, feedback_);
    for (int i = 0; i < 16 && panel.section() != SettingsPanel::Section::network; ++i)
        send_to(panel, press(Action::page_next));
    ASSERT_EQ(panel.section(), SettingsPanel::Section::network);
    send_to(panel, nav(Direction::right));
    EXPECT_EQ(send_to(panel, press(Action::confirm)), SettingsPanel::Change::stop_install);
    // Stop installing, Add, Find, Look for games again, then the drive.
    for (int i = 0; i < 4; ++i)
        send_to(panel, nav(Direction::down));
    send_to(panel, press(Action::confirm)); // the drive's page
    EXPECT_EQ(panel.share_id(), "s1");
    for (int i = 0; i < 10; ++i)
        send_to(panel, nav(Direction::down));
    send_to(panel, nav(Direction::up));
    EXPECT_EQ(send_to(panel, press(Action::confirm)), SettingsPanel::Change::install_game);
}

// Under the drives, this console's own: the switch, its user name and its
// password, each one a change for the screen.
TEST_F(OrionSettings, SharesThisConsolesDrives)
{
    SettingsPanel panel;
    hui::orion::ConsoleShare share;
    share.user = "ps5";
    panel.set_console_share(share, "", "192.168.1.30");
    panel.open(look_, prefs_, feedback_);
    for (int i = 0; i < 16 && panel.section() != SettingsPanel::Section::network; ++i)
        send_to(panel, press(Action::page_next));
    ASSERT_EQ(panel.section(), SettingsPanel::Section::network);
    send_to(panel, nav(Direction::right));
    for (int i = 0; i < 10; ++i)
        send_to(panel, nav(Direction::down));
    // From the bottom: Password, User name, then the switch.
    EXPECT_EQ(send_to(panel, press(Action::confirm)), SettingsPanel::Change::type_console_password);
    send_to(panel, nav(Direction::up));
    EXPECT_EQ(send_to(panel, press(Action::confirm)), SettingsPanel::Change::type_console_user);
    send_to(panel, nav(Direction::up));
    EXPECT_EQ(send_to(panel, press(Action::confirm)), SettingsPanel::Change::console_share);
    EXPECT_TRUE(panel.console_share().on);
}

} // namespace
