// ORION Media Center - Tests: a library with a large RomM catalogue listed whole.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concept_fixture.hpp"
#include "concepts/concepts.hpp"
#include "library/emulators.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{

using hui::Action;
using hui::Direction;

class RommStress : public hui::testing::ConceptFixture
{
};

// Fourteen thousand server games on a few systems: the library lists them
// up to its cap (each is a title in memory; a whole large catalogue ran
// ORION out of memory on a console), every tab draws, input works.
TEST_F(RommStress, ListsALargeCatalogueUpToItsCap)
{
    hui::library::ScanOptions options;
    options.roots = {std::string(HUI_SOURCE_DIR) + "/tests/fixtures/retroarch/homebrew"};
    library_.titles = hui::library::scan(options);
    library_.scanned = true;
    std::vector<hui::library::RemoteGame> remote;
    const char *const systems[] = {"gba", "nds", "snes", "psp", "n64", "psx"};
    for (int i = 0; i < 14000; ++i)
    {
        hui::library::RemoteGame game;
        game.system_folder = systems[i % 6];
        game.name = "Game " + std::to_string(i);
        game.path =
            "/data/orion/romm/games/" + game.system_folder + "/Game " + std::to_string(i) + ".zip";
        game.remote = "http://nas:3000/api/roms/" + std::to_string(i) + "/content/x.zip";
        game.cover = "http://nas:3000/c/" + std::to_string(i) + ".png";
        remote.push_back(std::move(game));
    }
    hui::library::add_emulator_games(library_, {}, "", false, {}, remote, false);
    const auto remote_listed =
        std::count_if(library_.titles.begin(), library_.titles.end(),
                      [](const hui::library::Title &title) { return !title.remote.empty(); });
    EXPECT_EQ(remote_listed, 6000);
    auto design = hui::concepts::make_library(context_);
    for (int tab = 0; tab < 12; ++tab)
    {
        send(*design, press(Action::page_next));
        for (int i = 0; i < 6; ++i)
            send(*design, nav(Direction::down));
        send(*design, press(Action::jump_next));
        expect_drawable(*design);
    }
    // And the library listed again over it, as Settings does.
    hui::library::add_emulator_games(library_, {}, "", false, {}, remote, false);
    ++library_.generation;
    for (int i = 0; i < 4; ++i)
    {
        send(*design, hui::InputFrame());
        expect_drawable(*design);
    }
}

} // namespace
