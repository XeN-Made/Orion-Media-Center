// ps5-homebrew-ui - Behaviour tests for the Library design.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concept_fixture.hpp"
#include "concepts/concepts.hpp"
#include "core/save_file.hpp"
#include "library/favourites.hpp"

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

TEST_F(LibraryDesign, OptionsCyclesTheSort)
{
    auto design = with_fixture();
    for (int i = 0; i < 4; ++i)
    {
        send(*design, press(hui::Action::menu));
        EXPECT_TRUE(played(Cue::tab)) << i;
        expect_drawable(*design);
    }
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
