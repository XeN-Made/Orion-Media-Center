// ps5-homebrew-ui - Behaviour tests for the Library design.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concept_fixture.hpp"
#include "concepts/concepts.hpp"
#include "core/save_file.hpp"
#include "library/favourites.hpp"
#include "library/emulators.hpp"
#include "orion/prefs.hpp"

#include <cstdio>

#include <string>

namespace
{

using hui::Direction;
using hui::audio::Cue;

class LibraryDesign : public hui::testing::ConceptFixture
{
  protected:
    // The invented library of tests/fixtures/library: seven titles, one row.
    std::unique_ptr<hui::app::Concept> with_fixture()
    {
        const std::string base = std::string(HUI_SOURCE_DIR) + "/tests/fixtures/library";
        hui::library::ScanOptions options;
        options.roots = {base + "/homebrew"};
        options.icon_cache = base + "/icon-cache";
        library_.titles = hui::library::scan(options);
        library_.scanned = true;
        return hui::concepts::make_library(context_);
    }
};

TEST_F(LibraryDesign, BrowsingTicksAndTheEdgesRefuse)
{
    auto design = with_fixture();
    ASSERT_EQ(library_.titles.size(), 7u);
    send(*design, nav(Direction::right));
    EXPECT_TRUE(played(Cue::focus));
    send(*design, nav(Direction::left));
    EXPECT_TRUE(played(Cue::focus));
    send(*design, nav(Direction::left));
    EXPECT_TRUE(played(Cue::error));
    EXPECT_FALSE(played(Cue::focus));
    // Seven titles fill one row of seven: there is nothing below.
    send(*design, nav(Direction::down));
    EXPECT_TRUE(played(Cue::error));
}

TEST_F(LibraryDesign, AHeldDirectionAtTheEdgeStaysQuiet)
{
    auto design = with_fixture();
    hui::InputFrame held = nav(Direction::left);
    held.nav_repeat = true;
    send(*design, held);
    EXPECT_TRUE(last_cues_.empty());
}

TEST_F(LibraryDesign, DrawsEveryTitleAndTheFocusedDetails)
{
    auto design = with_fixture();
    for (int i = 0; i < 7; ++i)
    {
        expect_drawable(*design);
        send(*design, nav(Direction::right));
    }
}

TEST_F(LibraryDesign, ShouldersTurnTheShelves)
{
    auto design = with_fixture();
    // All, Games, Homebrew, Media: the Media shelf holds one title.
    for (int i = 0; i < 3; ++i)
    {
        send(*design, press(hui::Action::page_next));
        EXPECT_TRUE(played(Cue::tab)) << i;
    }
    send(*design, nav(Direction::right));
    EXPECT_TRUE(played(Cue::error));
    // Favourites is empty: browsing there is silent, and past it is the edge.
    send(*design, press(hui::Action::page_next));
    send(*design, nav(Direction::right));
    EXPECT_TRUE(last_cues_.empty());
    expect_drawable(*design);
    send(*design, press(hui::Action::page_next));
    EXPECT_TRUE(played(Cue::error));
    // L2 / R2 turn them too.
    send(*design, press(hui::Action::jump_prev));
    EXPECT_TRUE(played(Cue::tab));
}

TEST_F(LibraryDesign, SquareStarsATitleAndSavesIt)
{
    const std::string dir = ::testing::TempDir() + "orion-design-state";
    ASSERT_TRUE(hui::save::ensure_directory(dir));
    std::remove((dir + "/favourites.json").c_str());
    library_.state_dir = dir;
    auto design = with_fixture();
    send(*design, press(hui::Action::west));
    EXPECT_TRUE(played(Cue::favorite_on));
    // The first title by name is Ciel Ouvert.
    EXPECT_EQ(hui::library::load_favourites(dir), (std::vector<std::string>{"PPSA90108"}));
    expect_drawable(*design);
    send(*design, press(hui::Action::west));
    EXPECT_TRUE(played(Cue::favorite_off));
    EXPECT_TRUE(hui::library::load_favourites(dir).empty());
}

TEST_F(LibraryDesign, R3OpensSettingsAndBackClosesThem)
{
    auto design = with_fixture();
    send(*design, press(hui::Action::r3));
    EXPECT_FALSE(last_cues_.empty());
    expect_drawable(*design);
    // The settings have the input: shoulders step through its sections.
    send(*design, press(hui::Action::page_next));
    EXPECT_TRUE(played(Cue::tab));
    send(*design, nav(Direction::down));
    EXPECT_TRUE(played(Cue::focus));
    send(*design, press(hui::Action::back));
    expect_drawable(*design);
    // Closed again: the grid has the input back.
    send(*design, nav(Direction::right));
    EXPECT_TRUE(played(Cue::focus));
}

TEST_F(LibraryDesign, EveryTabHasAPageInSettings)
{
    auto design = with_fixture();
    send(*design, press(hui::Action::r3));
    send(*design, press(hui::Action::page_next)); // Library -> Tabs
    send(*design, nav(Direction::right));         // into the rows: "New tab"
    send(*design, nav(Direction::down));          // "All"
    send(*design, nav(Direction::down));          // "Games"
    send(*design, press(hui::Action::confirm));   // its page
    expect_drawable(*design);
    // The page's first row shows or hides the tab; Cross on it flips it.
    send(*design, press(hui::Action::confirm));
    expect_drawable(*design);
    send(*design, press(hui::Action::back)); // back to the list of tabs
    EXPECT_TRUE(played(Cue::back));
    send(*design, press(hui::Action::back)); // back to the sections
    send(*design, press(hui::Action::back)); // closed
    expect_drawable(*design);
    send(*design, nav(Direction::right)); // the grid has the input again
    EXPECT_TRUE(played(Cue::focus));
}

TEST_F(LibraryDesign, CrossMovesATitleToACustomTabAndItIsRemembered)
{
    const std::string dir = ::testing::TempDir() + "orion-design-tabs";
    ASSERT_TRUE(hui::save::ensure_directory(dir));
    hui::orion::LibraryPrefs prefs;
    const std::string rpgs = prefs.add_tab("RPGs").id;
    ASSERT_TRUE(hui::orion::save_prefs(dir, prefs));
    std::remove((dir + "/favourites.json").c_str());
    library_.state_dir = dir;
    auto design = with_fixture();

    // Options opens the menu on Ciel Ouvert: Launch, Favourite, then Move to RPGs.
    send(*design, press(hui::Action::menu));
    EXPECT_FALSE(last_cues_.empty());
    send(*design, nav(Direction::down));
    send(*design, nav(Direction::down));
    send(*design, press(hui::Action::confirm));
    expect_drawable(*design);
    const hui::orion::LibraryPrefs saved = hui::orion::load_prefs(dir);
    ASSERT_NE(saved.placement("PPSA90108"), nullptr);
    EXPECT_EQ(saved.placement("PPSA90108")->id, rpgs);

    // The RPGs tab (after the five shelves) holds just that title.
    for (int i = 0; i < 5; ++i)
        send(*design, press(hui::Action::page_next));
    send(*design, nav(Direction::right));
    EXPECT_TRUE(played(Cue::error));
    EXPECT_EQ(hui::orion::load_prefs(dir).last_tab, "tab:" + rpgs);

    // A new Library opens on that tab again.
    auto again = hui::concepts::make_library(context_);
    send(*again, nav(Direction::right));
    EXPECT_TRUE(played(Cue::error));
}

TEST_F(LibraryDesign, LaunchSaysWhatHappenedAndLogsIt)
{
    const std::string dir = ::testing::TempDir() + "orion-design-launch";
    ASSERT_TRUE(hui::save::ensure_directory(dir));
    std::remove((dir + "/launch.txt").c_str());
    library_.state_dir = dir;
    auto design = with_fixture();
    // Cross launches: on a PC there is no helper to hand to, so nothing is
    // launched, the request is withdrawn and ORION stays open.
    send(*design, press(hui::Action::confirm));
    EXPECT_TRUE(played(Cue::error));
    expect_drawable(*design);
    std::string log;
    ASSERT_TRUE(hui::save::read_file(dir + "/launch.txt", &log));
    EXPECT_NE(log.find("PPSA90108"), std::string::npos);
    EXPECT_NE(log.find("not launched"), std::string::npos);
    std::string request;
    EXPECT_FALSE(hui::save::read_file(dir + "/launch-request.txt", &request));
    send(*design, idle(), 2.0f);
    EXPECT_FALSE(context_.exit_requested);
}

TEST_F(LibraryDesign, HoldingCircleClosesOrionButATapDoesNot)
{
    auto design = with_fixture();
    // A tap of Circle is the grid's back, nothing more.
    send(*design, press(hui::Action::back), 2.0f);
    EXPECT_FALSE(context_.exit_requested);
    // Held: the progress shows, then ORION asks to close.
    hui::InputFrame hold = press(hui::Action::back);
    send(*design, hold, 0.0f);
    hold.pressed = 0;
    for (int frame = 0; frame < 40; ++frame)
        send(*design, hold, 0.0f);
    EXPECT_FALSE(context_.exit_requested);
    expect_drawable(*design);
    for (int frame = 0; frame < 60; ++frame)
        send(*design, hold, 0.0f);
    EXPECT_TRUE(context_.exit_requested);
}

TEST_F(LibraryDesign, CircleHeldToCloseSettingsDoesNotCloseOrion)
{
    auto design = with_fixture();
    send(*design, press(hui::Action::r3));
    hui::InputFrame hold = press(hui::Action::back);
    send(*design, hold, 0.0f); // closes Settings
    hold.pressed = 0;
    for (int frame = 0; frame < 120; ++frame)
        send(*design, hold, 0.0f);
    EXPECT_FALSE(context_.exit_requested);
}

// The invented library plus the invented RetroArch: two systems, three games.
class LibraryWithRetroArch : public LibraryDesign
{
  protected:
    std::unique_ptr<hui::app::Concept> with_retroarch()
    {
        const std::string base = std::string(HUI_SOURCE_DIR) + "/tests/fixtures";
        hui::library::ScanOptions options;
        options.roots = {base + "/library/homebrew", base + "/retroarch/homebrew"};
        options.icon_cache = base + "/library/icon-cache";
        library_.titles = hui::library::scan(options);
        for (const hui::library::Title &title : std::vector<hui::library::Title>(library_.titles))
        {
            if (hui::library::is_retroarch(title))
                library_.emulator = hui::library::scan_retroarch(title, &library_.titles);
        }
        library_.scanned = true;
        return hui::concepts::make_library(context_);
    }
};

TEST_F(LibraryWithRetroArch, TheSystemsShareTheEmulatorsTab)
{
    auto design = with_retroarch();
    ASSERT_EQ(library_.emulator.systems.size(), 2u);
    // All, Games, Homebrew, Media, Favourites, Emulators: DS first under it.
    for (int i = 0; i < 5; ++i)
        send(*design, press(hui::Action::page_next));
    send(*design, nav(Direction::right));
    EXPECT_TRUE(played(Cue::error)); // DS holds one game
    // R2: GBA, which holds two.
    send(*design, press(hui::Action::jump_next));
    send(*design, nav(Direction::right));
    EXPECT_TRUE(played(Cue::focus));
    expect_drawable(*design);
    // Past GBA there is no system, and past Emulators no tab.
    send(*design, press(hui::Action::jump_next));
    EXPECT_TRUE(played(Cue::error));
    send(*design, press(hui::Action::page_next));
    EXPECT_TRUE(played(Cue::error));
}

TEST_F(LibraryWithRetroArch, EachSystemGetsATabOfItsGames)
{
    const std::string dir = ::testing::TempDir() + "orion-design-ungrouped";
    ASSERT_TRUE(hui::save::ensure_directory(dir));
    hui::orion::LibraryPrefs prefs;
    prefs.group_systems = false;
    ASSERT_TRUE(hui::orion::save_prefs(dir, prefs));
    library_.state_dir = dir;
    auto design = with_retroarch();
    ASSERT_EQ(library_.emulator.systems.size(), 2u);
    // All, Games, Homebrew, Media, Favourites, DS, GBA: GBA holds two games.
    for (int i = 0; i < 6; ++i)
        send(*design, press(hui::Action::page_next));
    send(*design, nav(Direction::right));
    EXPECT_TRUE(played(Cue::focus));
    send(*design, nav(Direction::right));
    EXPECT_TRUE(played(Cue::error));
    expect_drawable(*design);
    // Past GBA there is nothing.
    send(*design, press(hui::Action::page_next));
    EXPECT_TRUE(played(Cue::error));
}

TEST_F(LibraryWithRetroArch, TheConsoleShelvesStayFreeOfRoms)
{
    auto design = with_retroarch();
    // Games: the four invented games, not the three ROMs.
    send(*design, press(hui::Action::page_next));
    for (int i = 0; i < 3; ++i)
    {
        send(*design, nav(Direction::right));
        EXPECT_TRUE(played(Cue::focus)) << i;
    }
    send(*design, nav(Direction::right));
    EXPECT_TRUE(played(Cue::error));
}

TEST_F(LibraryWithRetroArch, TurningASystemOffTakesItsTabAway)
{
    const std::string dir = ::testing::TempDir() + "orion-design-systems";
    ASSERT_TRUE(hui::save::ensure_directory(dir));
    hui::orion::LibraryPrefs prefs;
    prefs.set_system_enabled("Nintendo - Game Boy Advance", false);
    ASSERT_TRUE(hui::orion::save_prefs(dir, prefs));
    library_.state_dir = dir;
    auto design = with_retroarch();
    for (int i = 0; i < 5; ++i)
        send(*design, press(hui::Action::page_next)); // ... to DS
    send(*design, press(hui::Action::page_next));
    EXPECT_TRUE(played(Cue::error)); // no GBA after DS
}

TEST_F(LibraryWithRetroArch, LaunchingARomNamesTheCoreAndTheGame)
{
    const std::string dir = ::testing::TempDir() + "orion-design-rom-launch";
    ASSERT_TRUE(hui::save::ensure_directory(dir));
    std::remove((dir + "/launch.txt").c_str());
    library_.state_dir = dir;
    auto design = with_retroarch();
    for (int i = 0; i < 6; ++i)
        send(*design, press(hui::Action::page_next));
    send(*design, press(hui::Action::confirm));
    std::string log;
    ASSERT_TRUE(hui::save::read_file(dir + "/launch.txt", &log));
    EXPECT_NE(log.find("Comet Post"), std::string::npos); // first by name on GBA
    EXPECT_NE(log.find("not on a console"), std::string::npos);
    expect_drawable(*design);
}

TEST_F(LibraryDesign, ItStartsOnAllWhenAskedNotToRemember)
{
    const std::string dir = ::testing::TempDir() + "orion-design-noreopen";
    ASSERT_TRUE(hui::save::ensure_directory(dir));
    hui::orion::LibraryPrefs prefs;
    prefs.last_tab = "media";
    prefs.reopen_last_tab = false;
    ASSERT_TRUE(hui::orion::save_prefs(dir, prefs));
    library_.state_dir = dir;
    auto design = with_fixture();
    // All has seven titles in a row, so right moves.
    send(*design, nav(Direction::right));
    EXPECT_TRUE(played(Cue::focus));
}

TEST_F(LibraryDesign, TriangleOpensTheKeyboardAndCircleClosesIt)
{
    auto design = with_fixture();
    send(*design, press(hui::Action::north));
    EXPECT_TRUE(played(Cue::modal_open));
    expect_drawable(*design);
    // Confirm types the focused key, "1"; the search matches IDs as well as
    // names, so the titles whose ID holds a 1 remain.
    send(*design, press(hui::Action::confirm));
    expect_drawable(*design);
    send(*design, press(hui::Action::back));
    EXPECT_TRUE(played(Cue::modal_close));
    // With the keyboard closed, Circle clears the search; a second Circle has
    // nothing to clear and is the grid's ordinary back.
    send(*design, press(hui::Action::back));
    EXPECT_TRUE(played(Cue::erase));
    send(*design, press(hui::Action::back));
    EXPECT_FALSE(played(Cue::erase));
    send(*design, nav(Direction::right));
    EXPECT_TRUE(played(Cue::focus));
}

TEST_F(LibraryDesign, AnEmptyLibraryShowsWhereItLookedAndIgnoresInput)
{
    library_.scanned = true;
    auto design = hui::concepts::make_library(context_);
    send(*design, nav(Direction::right));
    EXPECT_TRUE(last_cues_.empty());
    expect_drawable(*design);
}

} // namespace
