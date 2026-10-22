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
    // Library, Tabs, Emulators; into its rows: Scan, then Porpoise.
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

} // namespace
