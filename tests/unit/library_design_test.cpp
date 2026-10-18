// ps5-homebrew-ui - Behaviour tests for the Library design.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concept_fixture.hpp"
#include "concepts/concepts.hpp"

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

TEST_F(LibraryDesign, AnEmptyLibraryShowsWhereItLookedAndIgnoresInput)
{
    library_.scanned = true;
    auto design = hui::concepts::make_library(context_);
    send(*design, nav(Direction::right));
    EXPECT_TRUE(last_cues_.empty());
    expect_drawable(*design);
}

} // namespace
