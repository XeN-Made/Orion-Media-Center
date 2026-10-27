// ORION Media Center - Tests: the RomM Games screen's lists, and picking games on it.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/romm_browser.hpp"

#include "component_fixture.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace hui
{
namespace
{

library::Title retroarch()
{
    library::ScanOptions options;
    options.roots = {std::string(HUI_SOURCE_DIR) + "/tests/fixtures/retroarch/homebrew"};
    const std::vector<library::Title> titles = library::scan(options);
    return titles.empty() ? library::Title() : titles.front();
}

orion::RommGame game(long long id, const char *slug, const char *name, const char *file)
{
    orion::RommGame out;
    out.id = id;
    out.platform_id = 1;
    out.fs_slug = slug;
    out.name = name;
    out.fs_name = file;
    out.size = 4u << 20;
    return out;
}

// What the screen lists: by system, the console's own games known, the
// saves counted; a system ORION cannot play left out.
TEST(RommBrowser, ListsTheServersGamesBySystem)
{
    std::vector<library::Title> roms;
    const library::Emulator emulator = library::scan_retroarch(retroarch(), &roms);
    ASSERT_FALSE(roms.empty());
    const library::Title &mine = roms.front();
    const std::string folder = mine.system == "Nintendo - Nintendo DS" ? "nds" : "gba";
    const std::string file = mine.path.substr(mine.path.rfind('/') + 1);
    std::vector<orion::RommGame> games = {
        game(1, folder.c_str(), "Another Name", file.c_str()), // the console has it
        game(2, folder.c_str(), "Brand New", "Brand New.bin"),
        game(3, folder.c_str(), "Antique", "Antique.bin"),
        game(4, "not-a-system", "Nowhere", "x.bin"),
    };
    std::vector<orion::RommSave> saves(2);
    saves[0].id = 9;
    saves[0].rom_id = 3;
    saves[1].id = 10;
    saves[1].rom_id = 3;
    saves[1].state = true;
    const library::OwnedGames owned(roms);
    const std::vector<orion::RommEntry> entries =
        orion::romm_entries(games, saves, emulator, owned, "http://nas:3000", "/tmp/orion-none");
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_TRUE(entries[0].owned);
    EXPECT_FALSE(entries[1].owned);
    EXPECT_EQ(entries[2].saves, 1);
    EXPECT_EQ(entries[2].states, 1);
    EXPECT_EQ(entries[1].url, "http://nas:3000/api/roms/2/content/Brand%20New.bin");

    const std::vector<std::string> systems = orion::romm_entry_systems(entries);
    ASSERT_EQ(systems.size(), 1u);
    // Not on the console: by name. With saves: the one. All: the three.
    EXPECT_EQ(orion::romm_view(entries, systems[0], orion::RommFilter::missing),
              (std::vector<int>{2, 1}));
    EXPECT_EQ(orion::romm_view(entries, systems[0], orion::RommFilter::saves),
              (std::vector<int>{2}));
    EXPECT_EQ(orion::romm_view(entries, systems[0], orion::RommFilter::all).size(), 3u);
}

class RommBrowserScreen : public hui::testing::ComponentFixture
{
};

// Cross ticks a game (not one the console has), Square hands the ticked
// ones over, Triangle changes the filter, Circle closes.
TEST_F(RommBrowserScreen, TicksAndDownloadsThePickedGames)
{
    std::vector<orion::RommEntry> entries(3);
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        entries[i].game.id = static_cast<long long>(i + 1);
        entries[i].game.name = "Game " + std::to_string(i + 1);
        entries[i].short_name = "GBA";
    }
    entries[2].owned = true;
    orion::RommBrowser browser;
    browser.open(entries, true, feedback_);
    browser.handle(press(Action::jump_next), feedback_); // past Saves, to GBA
    for (int i = 0; i < 30; ++i)
        browser.update(1.0f / 60.0f);
    // Nothing ticked: Square refuses.
    EXPECT_EQ(browser.handle(press(Action::west), feedback_), orion::RommBrowser::Choice::none);
    EXPECT_EQ(browser.handle(press(Action::confirm), feedback_),
              orion::RommBrowser::Choice::none); // ticks Game 1
    EXPECT_EQ(browser.handle(press(Action::west), feedback_), orion::RommBrowser::Choice::download);
    const std::vector<orion::RommEntry> picked = browser.take_picked();
    ASSERT_EQ(picked.size(), 1u);
    EXPECT_EQ(picked[0].game.id, 1);
    EXPECT_TRUE(browser.take_picked().empty());      // the ticks went with them
    browser.handle(press(Action::north), feedback_); // another filter
    browser.update(kFrame);
    ui::Canvas frame = canvas();
    browser.draw(frame);
    expect_drawn("the RomM Games screen");
    EXPECT_EQ(browser.handle(press(Action::back), feedback_), orion::RommBrowser::Choice::closed);
}

} // namespace
} // namespace hui
