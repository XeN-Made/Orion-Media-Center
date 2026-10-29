// ORION Media Center - What's new: the changes of the version just installed, shown once.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/detail_list.hpp"
#include "ui/components/sheet.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hui::orion
{

// The notes ORION ships (assets/whats-new.txt):
//
//   # 01.106.000
//   ## New
//   Title music volume | Settings, Sound: how loud a game's own music plays.
//   ## Fixed
//   Icons no longer show another title's picture at start.
//
// "# " gives the version the notes are for; "## " starts a section; a line
// with " | " is a name and what it does, any other line a change on its own.
struct WhatsNewNotes
{
    std::string version;
    std::vector<ui::DetailItem> items;
};
bool parse_whats_new(std::string_view text, WhatsNewNotes *notes);

// Whether the notes are shown as ORION opens: once per version, after an
// update (seen: the version they were last shown for, empty before any). A
// new install has the setup to show it around instead.
bool show_whats_new(const WhatsNewNotes &notes, const std::string &version, const std::string &seen,
                    bool set_up);

// The notes in a large panel in the middle of the screen, read by
// scrolling; Circle closes, as the hint at its foot says.
//
//   WhatsNew whats_new;
//   whats_new.open(notes, feedback);
//   ...
//   if (whats_new.handle(input, feedback)) ... closed
class WhatsNew
{
  public:
    WhatsNew();
    void set_theme(const ui::Theme &theme, bool reduced_motion);
    void open(const WhatsNewNotes &notes, ui::Feedback &feedback);
    void close(ui::Feedback &feedback);
    bool is_open() const
    {
        return sheet_.is_open();
    }
    // True when it closed.
    bool handle(const InputFrame &input, ui::Feedback &feedback);
    void update(float dt);
    void draw(ui::Canvas &canvas) const;

  private:
    ui::Sheet sheet_;
    ui::DetailList rows_;
};

} // namespace hui::orion
