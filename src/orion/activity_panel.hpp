// ORION Media Center - Activity: every install and download running, with its progress.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/sheet.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hui::orion
{

// One thing ORION is doing in the background: a game unpacking, a package
// going to the installer, box art, an update.
struct ActivityJob
{
    std::string text;       // "Installing Moon Garden  ·  1.2 GB of 31.8 GB"
    float progress = -1.0f; // 0..1, or below 0 when the end is not known
    std::string detail;     // under the bar: its speed and time left, or empty
};

// "24.3 MB/s  ·  about 26 min left": the average speed since a transfer
// started and the time that leaves at it (no estimate without a total).
// Empty in its first seconds, before there is a speed to tell.
std::string transfer_speed(std::uint64_t done, std::uint64_t total, double seconds);

// A sheet in the middle of the screen with every job, a bar under each, the
// way the console's Downloads lists them. L3 opens it from anywhere in
// ORION; Circle (or L3 again) closes it. The screen hands it the jobs each
// frame.
//
//   orion::ActivityPanel activity;
//   activity.set_theme(theme, reduced_motion);
//   activity.set_jobs(jobs());          // every frame
//   if (input.is_pressed(Action::l3)) activity.open(feedback);
//   if (activity.handle(input, feedback)) activity.close(feedback);
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
    void update(float dt);
    void draw(ui::Canvas &canvas) const;

  private:
    void draw_content(ui::Canvas &canvas, const gfx::Rect &area) const;

    ui::Sheet sheet_;
    std::vector<ActivityJob> jobs_;
    float clock_ = 0.0f;
    bool reduced_motion_ = false;
};

} // namespace hui::orion
