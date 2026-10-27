// ORION Media Center - Tests: the What's new notes and when they show.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/whats_new.hpp"

#include "core/save_file.hpp"

#include <gtest/gtest.h>

#include <string>

namespace hui
{
namespace
{

TEST(WhatsNew, ReadsTheNotes)
{
    orion::WhatsNewNotes notes;
    ASSERT_TRUE(orion::parse_whats_new("# 01.106.000\n"
                                       "## New\n"
                                       "Title music volume | Settings, Sound: how loud.\n"
                                       "\n"
                                       "## Fixed\n"
                                       "  Icons stay on their titles.  \n"
                                       "# 02.000.000\n", // a second version line is ignored
                                       &notes));
    EXPECT_EQ(notes.version, "01.106.000");
    ASSERT_EQ(notes.items.size(), 4u);
    EXPECT_TRUE(notes.items[0].header);
    EXPECT_EQ(notes.items[0].label, "New");
    EXPECT_EQ(notes.items[1].label, "Title music volume");
    EXPECT_EQ(notes.items[1].value, "Settings, Sound: how loud.");
    EXPECT_TRUE(notes.items[2].header);
    EXPECT_EQ(notes.items[3].label, "");
    EXPECT_EQ(notes.items[3].value, "Icons stay on their titles.");
    EXPECT_FALSE(orion::parse_whats_new("## New\nno version\n", &notes));
    EXPECT_FALSE(orion::parse_whats_new("# 01.106.000\n", &notes)); // nothing in it
}

// Once per version, after an update; never on a new install (the setup),
// nor for notes written for another version.
TEST(WhatsNew, ShowsOnceAfterAnUpdate)
{
    orion::WhatsNewNotes notes;
    ASSERT_TRUE(orion::parse_whats_new("# 01.106.000\nA change\n", &notes));
    EXPECT_TRUE(orion::show_whats_new(notes, "01.106.000", "", true));
    EXPECT_TRUE(orion::show_whats_new(notes, "01.106.000", "01.105.000", true));
    EXPECT_FALSE(orion::show_whats_new(notes, "01.106.000", "01.106.000", true)); // seen
    EXPECT_FALSE(orion::show_whats_new(notes, "01.106.000", "", false));          // new install
    EXPECT_FALSE(orion::show_whats_new(notes, "01.107.000", "", true));           // old notes
}

// The notes ORION ships are for a version and read whole.
TEST(WhatsNew, TheShippedNotesRead)
{
    std::string text;
    ASSERT_TRUE(save::read_file(std::string(HUI_SOURCE_DIR) + "/assets/whats-new.txt", &text));
    orion::WhatsNewNotes notes;
    ASSERT_TRUE(orion::parse_whats_new(text, &notes));
    EXPECT_EQ(notes.version.size(), 10u) << notes.version; // "01.106.000"
    EXPECT_GT(notes.items.size(), 3u);
}

} // namespace
} // namespace hui
