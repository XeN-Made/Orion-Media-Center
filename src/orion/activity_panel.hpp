// ORION Media Center - Activity: every install and download running, with its progress.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/sheet.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace hui::orion
{

enum class ActivityState : unsigned char
{
    running, // a bar, and the detail under it
    waiting, // queued behind the running one
    done,
    failed,
};

// One thing ORION is doing in the background: a game unpacking, a package
// going to the installer, box art, an update; or one waiting its turn, or
// one that ended this session.
struct ActivityJob
{
    std::string text;       // "Installing Moon Garden  ·  1.2 GB of 31.8 GB"
    float progress = -1.0f; // 0..1, or below 0 when the end is not known
    std::string detail;     // under the bar: its speed and time left, or empty
    ActivityState state = ActivityState::running;
};

// "24.3 MB/s  ·  about 26 min left": the average speed since a transfer
// started and the time that leaves at it (no estimate without a total).
// Empty in its first seconds, before there is a speed to tell.
std::string transfer_speed(std::uint64_t done, std::uint64_t total, double seconds);

// A sheet in the middle of the screen with every job, a bar under each, the
// way the console's Downloads lists them: the running ones, then the ones
// waiting, then the ones finished (a line each). Up and down scroll a list
// longer than the sheet; Square asks for the finished ones to be cleared.
// L3 opens it from anywhere in ORION; Circle (or L3 again) closes it. The
// screen hands it the jobs each frame.
//
//   orion::ActivityPanel activity;
//   activity.set_theme(theme, reduced_motion);
//   activity.set_jobs(jobs());          // every frame
//   if (input.is_pressed(Action::l3)) activity.open(feedback);
//   if (activity.handle(input, feedback)) activity.close(feedback);
//   if (activity.take_clear()) finished.clear();
//   activity.update(dt);
//   activity.draw(canvas);              // over everything but dialogs
class ActivityPanel
{
  public:
    ActivityPanel();
    void set_theme(const ui::Theme &theme, bool reduced_motion);
    void set_jobs(std::vector<ActivityJob> jobs)
    {
        jobs_ = std::move(jobs);
    }
    void open(ui::Feedback &feedback);
    void close(ui::Feedback &feedback);
    bool is_open() const
    {
        return sheet_.is_open();
    }
    bool visible() const
    {
        return sheet_.visible();
    }
    // True when it asked to close (Circle, or L3 again).
    bool handle(const InputFrame &input, ui::Feedback &feedback);
    // True once after Square asked for the finished jobs to go.
    bool take_clear()
    {
        return std::exchange(clear_asked_, false);
    }
    int first_shown() const
    {
        return first_;
    }
    void update(float dt);
    void draw(ui::Canvas &canvas) const;

  private:
    void draw_content(ui::Canvas &canvas, const gfx::Rect &area) const;
    bool has_finished() const;
    // The last first row that still fills the sheet: scrolling stops there.
    int last_first() const;

    ui::Sheet sheet_;
    std::vector<ActivityJob> jobs_;
    ui::Pulse pulse_;
    float clock_ = 0.0f;
    // The room for rows, known once the sheet has laid out: kept as it draws.
    mutable float rows_height_ = 600.0f;
    int first_ = 0; // the row at the top
    bool clear_asked_ = false;
    bool reduced_motion_ = false;
};

} // namespace hui::orion
