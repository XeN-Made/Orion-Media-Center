// ORION Media Center - A title's details page: its cover, its facts, Play and Favourite.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/detail_list.hpp"
#include "ui/components/sheet.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hui::orion
{

// What the details show, worked out by the library (it knows the players,
// the play records and the drives). Empty fields leave their row out.
struct TitleFacts
{
    std::string name;
    std::string id;          // a console title's ID ("PPSA01234")
    std::string file;        // a game for an emulator: its file's name
    std::string version;     // "01.000.010"
    std::string kind;        // "Game", "Homebrew", "Media", or the system ("GBA")
    std::string plays_with;  // "PS5", or the emulator ("RetroArch, mGBA")
    std::string kept_as;     // "Folder", "Image", "Installed"
    std::string drive;       // "USB drive 2"
    std::string path;        // the folder, image or game file
    std::string play_time;   // "12 h 4 m"; empty: not played yet
    std::string last_played; // "yesterday"
    std::string added;       // "3 weeks ago"
    bool favourite = false;
    bool folder = false;       // path is a folder: its size is added up when the details open
    std::uint32_t texture = 0; // the cover
    float aspect = 1.0f;       // its width over its height
};

// The rows, in order: Play and the favourite switch first (both focusable),
// then what the title is and where it lives. size is shown as given ("…"
// while a folder is being added up). Tags: kDetailsPlay, kDetailsFavourite.
inline constexpr int kDetailsPlay = 1;
inline constexpr int kDetailsFavourite = 2;
inline constexpr int kDetailsSize = 3;
std::vector<ui::DetailItem> title_detail_rows(const TitleFacts &facts, const std::string &size);

// The bytes under a folder, its folders included, at most max_entries files
// looked at (a huge folder stops there; *complete says whether it did not).
std::uint64_t folder_bytes(const std::string &path, int max_entries, bool *complete);

// The details as a sheet from the right: the cover, then the rows. Cross on
// Play starts the title, on Favourite stars it; Circle closes.
//
//   TitleDetails details;
//   details.open(facts, theme, feedback);
//   ...
//   switch (details.handle(input, feedback)) { case TitleDetails::Choice::play: ... }
//   details.update(dt);
//   details.draw(canvas);
class TitleDetails
{
  public:
    enum class Choice : std::uint8_t
    {
        none,
        play,
        favourite,
        closed,
    };

    TitleDetails();
    void set_theme(const ui::Theme &theme, bool reduced_motion);
    void open(TitleFacts facts, ui::Feedback &feedback);
    void close(ui::Feedback &feedback);
    bool is_open() const
    {
        return sheet_.is_open();
    }
    bool visible() const
    {
        return sheet_.visible();
    }
    // The favourite row follows the title's star after a change.
    void set_favourite(bool favourite);

    Choice handle(const InputFrame &input, ui::Feedback &feedback);
    void update(float dt);
    void draw(ui::Canvas &canvas) const;

  private:
    void layout();
    void draw_content(ui::Canvas &canvas, const gfx::Rect &area, float opacity) const;

    // A folder's size, added up on a thread of its own; the sheet may close
    // (or open on another title) first, so the result is shared.
    struct Count
    {
        std::atomic<bool> done{false};
        std::atomic<std::uint64_t> bytes{0};
        std::atomic<bool> complete{true};
    };

    ui::Sheet sheet_;
    ui::DetailList rows_;
    TitleFacts facts_;
    std::shared_ptr<Count> count_;
    bool size_shown_ = true;
};

} // namespace hui::orion
