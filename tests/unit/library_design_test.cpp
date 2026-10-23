// ORION Media Center - Behaviour tests for the Library design.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concept_fixture.hpp"
#include "concepts/concepts.hpp"
#include "core/save_file.hpp"
#include "library/favourites.hpp"
#include "library/emulators.hpp"
#include "orion/prefs.hpp"

#include <cstdio>
#include <filesystem>

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

// A console that has not been set up starts on the setup; Skip setup ends it
// for good and the library takes the input again.
TEST_F(LibraryDesign, ANewConsoleStartsOnTheSetupAndCanSkipIt)
{
    const std::string dir = ::testing::TempDir() + "orion-design-setup-skip";
    ASSERT_TRUE(hui::save::ensure_directory(dir));
    std::remove((dir + "/library.json").c_str());
    library_.state_dir = dir;
    library_.offer_setup = true;
    auto design = with_fixture();
    send(*design, hui::InputFrame());
    expect_drawable(*design);
    // The shoulders belong to the setup now: no shelf turns.
    send(*design, press(hui::Action::page_next));
    EXPECT_FALSE(played(Cue::tab));
    // Get started, what was found, Skip setup: the last row.
    for (int i = 0; i < 4; ++i)
        send(*design, nav(Direction::down));
    send(*design, press(hui::Action::confirm));
    EXPECT_TRUE(hui::orion::load_prefs(dir).setup_done);
    send(*design, press(hui::Action::page_next));
    EXPECT_TRUE(played(Cue::tab));
}

TEST_F(LibraryDesign, TheSetupWalksThroughEveryStep)
{
    const std::string dir = ::testing::TempDir() + "orion-design-setup-walk";
    ASSERT_TRUE(hui::save::ensure_directory(dir));
    std::remove((dir + "/library.json").c_str());
    library_.state_dir = dir;
    library_.offer_setup = true;
    auto design = with_fixture();
    send(*design, hui::InputFrame());
    // Welcome: Get started is the first row.
    send(*design, press(hui::Action::confirm));
    // Look, Emulators, Box art, Library, Autostart: Continue is the last row.
    for (int step = 0; step < 5; ++step)
    {
        // Circle on a later step goes back one, Get started comes forward again.
        if (step == 2)
        {
            send(*design, press(hui::Action::back));
            EXPECT_TRUE(played(Cue::back));
            for (int i = 0; i < 10; ++i)
                send(*design, nav(Direction::down));
            send(*design, press(hui::Action::confirm));
        }
        for (int i = 0; i < 10; ++i)
            send(*design, nav(Direction::down));
        expect_drawable(*design);
        EXPECT_FALSE(hui::orion::load_prefs(dir).setup_done) << step;
        send(*design, press(hui::Action::confirm));
    }
    // Done: Start using ORION is the first row.
    send(*design, press(hui::Action::confirm));
    EXPECT_TRUE(hui::orion::load_prefs(dir).setup_done);
}

// With something played, the All tab has a Continue playing row between the
// covers and the sort picker: up from the first row reaches it, up again the
// picker, and down comes back the same way.
TEST_F(LibraryDesign, ContinuePlayingSitsAboveTheCovers)
{
    const std::string dir = ::testing::TempDir() + "orion-design-continue";
    ASSERT_TRUE(hui::save::ensure_directory(dir));
    std::remove((dir + "/library.json").c_str());
    ASSERT_TRUE(hui::save::write_atomic(dir + "/play-log.txt", "100 200 PPSA90101\n").empty());
    library_.state_dir = dir;
    auto design = with_fixture();
    send(*design, nav(Direction::up));
    EXPECT_TRUE(played(Cue::focus));
    // One title in the row: left and right refuse.
    send(*design, nav(Direction::right));
    EXPECT_TRUE(played(Cue::error));
    expect_drawable(*design);
    send(*design, nav(Direction::up)); // the sort picker
    EXPECT_TRUE(played(Cue::focus));
    send(*design, nav(Direction::down)); // the row again
    send(*design, nav(Direction::down)); // the covers
    send(*design, nav(Direction::right));
    EXPECT_TRUE(played(Cue::focus));
    // Off in the prefs, the row is gone: up goes straight to the picker.
    hui::orion::LibraryPrefs prefs;
    prefs.continue_row = false;
    ASSERT_TRUE(hui::orion::save_prefs(dir, prefs));
    auto without = with_fixture();
    send(*without, nav(Direction::up));
    EXPECT_TRUE(played(Cue::focus));
    send(*without, nav(Direction::up)); // nothing above the picker
    EXPECT_TRUE(played(Cue::error));
    std::remove((dir + "/play-log.txt").c_str());
}

// Without offer_setup (tests, the PC tour) the library starts as it is.
TEST_F(LibraryDesign, NoSetupUnlessTheAppOffersIt)
{
    auto design = with_fixture();
    send(*design, press(hui::Action::page_next));
    EXPECT_TRUE(played(Cue::tab));
}

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

    // Options opens the menu on Ciel Ouvert: Launch, Favourite, Hide, Rename,
    // then Move to RPGs.
    send(*design, press(hui::Action::menu));
    EXPECT_FALSE(last_cues_.empty());
    for (int i = 0; i < 4; ++i)
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

// Hide this title takes it off every shelf onto a Hidden tab at the end;
// Show this title brings it back, and the Hidden tab goes with the last one.
TEST_F(LibraryDesign, AHiddenTitleLeavesItsShelvesForTheHiddenTab)
{
    const std::string dir = ::testing::TempDir() + "orion-design-hidden";
    ASSERT_TRUE(hui::save::ensure_directory(dir));
    std::remove((dir + "/library.json").c_str());
    std::remove((dir + "/favourites.json").c_str());
    library_.state_dir = dir;
    auto design = with_fixture();
    // Options on the first title: Launch, Favourite, Hide.
    send(*design, press(hui::Action::menu));
    send(*design, nav(Direction::down));
    send(*design, nav(Direction::down));
    send(*design, press(hui::Action::confirm));
    const hui::orion::LibraryPrefs saved = hui::orion::load_prefs(dir);
    ASSERT_EQ(saved.hidden_titles.size(), 1u);
    const std::string hidden = saved.hidden_titles.front();
    // All has six titles now: five steps right, then the edge.
    for (int i = 0; i < 5; ++i)
    {
        send(*design, nav(Direction::right));
        EXPECT_TRUE(played(Cue::focus)) << i;
    }
    send(*design, nav(Direction::right));
    EXPECT_TRUE(played(Cue::error));
    // All, Games, Homebrew, Media, Favourites, Hidden: it holds that one title.
    for (int i = 0; i < 5; ++i)
        send(*design, press(hui::Action::page_next));
    EXPECT_EQ(hui::orion::load_prefs(dir).last_tab, "hidden");
    send(*design, nav(Direction::right));
    EXPECT_TRUE(played(Cue::error));
    // Show this title: no title is hidden, and the Hidden tab is gone.
    send(*design, press(hui::Action::menu));
    send(*design, nav(Direction::down));
    send(*design, nav(Direction::down));
    send(*design, press(hui::Action::confirm));
    EXPECT_TRUE(hui::orion::load_prefs(dir).hidden_titles.empty());
    // Its tab went with it: ORION is on All again, and R1 turns to Games.
    send(*design, press(hui::Action::page_next));
    EXPECT_TRUE(played(Cue::tab));
    expect_drawable(*design);
    EXPECT_TRUE(hui::library::is_item_id(hidden));
}

// Remove… on a game for an emulator deletes its file, but only after Cross
// has been held on Delete: a press, or letting go early, erases nothing.
TEST_F(LibraryDesign, DeletingAGameFileWantsCrossHeld)
{
    const std::string dir = ::testing::TempDir() + "orion-design-delete";
    ASSERT_TRUE(hui::save::ensure_directory(dir));
    std::remove((dir + "/library.json").c_str());
    const std::string file = dir + "/Doomed (USA).gba";
    ASSERT_TRUE(hui::save::write_atomic(file, "rom").empty());
    library_.state_dir = dir;
    auto design_without = with_fixture(); // fills library_.titles
    hui::library::Title rom;
    rom.id = "rom-000000000abc";
    rom.name = "Doomed";
    rom.system = "Nintendo - Game Boy Advance";
    rom.rom = file;
    rom.path = file;
    rom.source = hui::library::Source::image;
    library_.titles.push_back(rom);
    hui::library::System gba;
    gba.name = rom.system;
    gba.short_name = "GBA";
    gba.games = 1;
    library_.emulator.systems.push_back(gba);
    auto design = hui::concepts::make_library(context_);
    // All, Games, Homebrew, Media, Favourites, Emulators: Doomed alone.
    for (int i = 0; i < 5; ++i)
        send(*design, press(hui::Action::page_next));
    // Options: Launch, Favourite, Hide, Rename, Download box art again, Move
    // to a new tab, Remove….
    send(*design, press(hui::Action::menu));
    for (int i = 0; i < 6; ++i)
        send(*design, nav(Direction::down));
    send(*design, press(hui::Action::confirm)); // Remove…
    send(*design, press(hui::Action::confirm)); // Delete the game file…
    expect_drawable(*design);
    send(*design, nav(Direction::right)); // the dialog opens on Cancel
    // A press alone, then a short hold, then letting go: still there.
    send(*design, press(hui::Action::confirm), 0.0f);
    hui::InputFrame held = idle();
    held.held = hui::action_bit(hui::Action::confirm);
    for (int frame = 0; frame < 30; ++frame)
        send(*design, held, 0.0f);
    send(*design, idle(), 0.0f);
    EXPECT_TRUE(std::filesystem::exists(file));
    // Held for the whole second and a half: gone, and off its tab.
    send(*design, press(hui::Action::confirm), 0.0f);
    for (int frame = 0; frame < 100; ++frame)
        send(*design, held, 0.0f);
    EXPECT_FALSE(std::filesystem::exists(file));
    send(*design, nav(Direction::right));
    expect_drawable(*design);
}

// The launch helper removes a game while ORION is closed and leaves word in
// ORION's folder: a delete takes the title off every tab, a home-screen
// removal puts it on Hidden, and the word is read once.
TEST_F(LibraryDesign, AHelperRemovalIsAppliedWhenOrionOpens)
{
    const std::string dir = ::testing::TempDir() + "orion-design-removed";
    ASSERT_TRUE(hui::save::ensure_directory(dir));
    std::remove((dir + "/library.json").c_str());
    library_.state_dir = dir;
    library_.self_path = dir;
    ASSERT_TRUE(
        hui::save::write_atomic(dir + "/remove-result.txt", "erase 1 PPSA90101\t\n").empty());
    auto design = with_fixture();
    EXPECT_FALSE(std::filesystem::exists(dir + "/remove-result.txt"));
    // All holds six titles now: five steps right, then the edge.
    for (int i = 0; i < 5; ++i)
        send(*design, nav(Direction::right));
    send(*design, nav(Direction::right));
    EXPECT_TRUE(played(Cue::error));

    ASSERT_TRUE(
        hui::save::write_atomic(dir + "/remove-result.txt", "home 1 PPSA90103\t\n").empty());
    auto again = hui::concepts::make_library(context_);
    const hui::orion::LibraryPrefs prefs = hui::orion::load_prefs(dir);
    ASSERT_EQ(prefs.hidden_titles.size(), 1u);
    EXPECT_EQ(prefs.hidden_titles.front(), "PPSA90103");

    // A failure changes nothing.
    ASSERT_TRUE(hui::save::write_atomic(dir + "/remove-result.txt",
                                        "erase 0 PPSA90104\tShadowMountPlus said no\n")
                    .empty());
    auto third = hui::concepts::make_library(context_);
    EXPECT_EQ(hui::orion::load_prefs(dir).hidden_titles.size(), 1u);
    expect_drawable(*third);
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
    std::remove((dir + "/library.json").c_str()); // no emulator chosen from an earlier run
    library_.state_dir = dir;
    auto design = with_retroarch();
    // Emulators, then R2 from DS to GBA.
    for (int i = 0; i < 5; ++i)
        send(*design, press(hui::Action::page_next));
    send(*design, press(hui::Action::jump_next));
    // Two cores play GBA: the first start asks which, and starts nothing yet.
    send(*design, press(hui::Action::confirm));
    EXPECT_FALSE(std::filesystem::exists(dir + "/launch.txt"));
    expect_drawable(*design);
    send(*design, press(hui::Action::confirm)); // the first core
    std::string log;
    ASSERT_TRUE(hui::save::read_file(dir + "/launch.txt", &log));
    EXPECT_NE(log.find("Comet Post"), std::string::npos); // first by name on GBA
    EXPECT_NE(log.find("not on a console"), std::string::npos);
    // Kept: the next start goes straight on.
    EXPECT_FALSE(hui::orion::load_prefs(dir).game_players.empty());
    std::remove((dir + "/launch.txt").c_str());
    send(*design, press(hui::Action::confirm), 7.0f);
    EXPECT_TRUE(std::filesystem::exists(dir + "/launch.txt"));
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
