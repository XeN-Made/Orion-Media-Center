// ORION Media Center - The RomM Games screen: the server's games by system, picked and downloaded.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "library/emulators.hpp"
#include "orion/romm.hpp"
#include "ui/components/list.hpp"
#include "ui/components/sheet.hpp"
#include "ui/components/tabs.hpp"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace hui::orion
{

// Which of a system's games the screen lists.
enum class RommFilter : std::uint8_t
{
    missing, // not on the console (the default: what can be added)
    saves,   // with a save or a save state on RomM
    all,
};

// One game of the server, as the screen shows it.
struct RommEntry
{
    RommGame game;
    std::string system;      // ORION's name for it ("Nintendo - Game Boy Advance")
    std::string short_name;  // its tab ("GBA")
    std::string path;        // where it goes on the console
    std::string url;         // where it comes from
    bool owned = false;      // the console has it (another copy: OwnedGames)
    bool downloaded = false; // fetched from RomM already
    int saves = 0;
    int states = 0;
};

// The server's games ORION can play (a system it knows), with what the
// console has and what the player keeps on RomM. folder: where downloads go.
std::vector<RommEntry> romm_entries(const std::vector<RommGame> &games,
                                    const std::vector<RommSave> &saves,
                                    const library::Emulator &emulator,
                                    const library::OwnedGames &owned, const std::string &base,
                                    const std::string &folder);
// The systems that have games, by their short names.
std::vector<std::string> romm_entry_systems(const std::vector<RommEntry> &entries);
// One system's games the filter lets through, by name (indices into entries).
// An empty system: every system's. query, when given, keeps the names that
// hold it (case and accents aside).
std::vector<int> romm_view(const std::vector<RommEntry> &entries, const std::string &system,
                           RommFilter filter, const std::string &query = {});

// The screen: a sheet over the library. Its first tab, Saves, holds every
// game with a save or a state on RomM; then a tab per system. L2 and R2
// change tab, Triangle the filter, Options searches, Cross ticks a game,
// Square downloads the ticked ones, Circle closes.
//
//   RommBrowser browser;
//   browser.open(entries, feedback);
//   switch (browser.handle(input, feedback)) { case RommBrowser::Choice::download: ... }
class RommBrowser
{
  public:
    enum class Choice : std::uint8_t
    {
        none,
        closed,
        download, // picked(): the games to fetch
        search,   // the screen asks for words to look for (set_query)
    };

    RommBrowser();
    void set_theme(const ui::Theme &theme, bool reduced_motion);
    void open(std::vector<RommEntry> entries, ui::Feedback &feedback);
    void close(ui::Feedback &feedback);
    bool is_open() const
    {
        return sheet_.is_open();
    }
    Choice handle(const InputFrame &input, ui::Feedback &feedback);
    // The ticked games; the ticks are cleared.
    std::vector<RommEntry> take_picked();
    // Words to look for in the games' names; empty shows them all.
    void set_query(std::string query);
    const std::string &query() const
    {
        return query_;
    }
    void update(float dt);
    void draw(ui::Canvas &canvas) const;

  private:
    void show_system(int index);
    void rebuild_list();
    void draw_content(ui::Canvas &canvas, const gfx::Rect &area, float opacity) const;
    std::string filter_name() const;

    ui::Sheet sheet_;
    ui::TabBar tabs_;
    ui::ListView list_;
    std::vector<RommEntry> entries_;
    std::vector<std::string> systems_;
    std::vector<int> view_;
    RommFilter filter_ = RommFilter::missing;
    std::unordered_set<long long> picked_; // RomM's ids
    std::string query_;
    bool saves_tab_ = false; // the first tab is Saves (there are some)
    gfx::Rect area_;
};

} // namespace hui::orion
