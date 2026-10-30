// ORION Media Center - Activity: every install and download running, with its progress.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/activity_panel.hpp"

#include "core/i18n.hpp"
#include "ui/components/action_glyph.hpp"
#include "ui/components/component.hpp"
#include "ui/glyphs.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hui::orion
{

namespace
{

constexpr float kHintSize = 34.0f;
constexpr float kHintRoom = 62.0f; // under the jobs, for the hint
constexpr float kRow = 116.0f;     // the name, the bar, and the speed under it
constexpr float kBarRow = 84.0f;   // the name and the bar
constexpr float kLine = 56.0f;     // waiting or finished: the name and a word
constexpr float kText = 26.0f;
constexpr float kDetail = 22.0f;

// Under this, an average says more about the start than the speed.
constexpr double kFirstSeconds = 3.0;

float row_height(const ActivityJob &job)
{
    if (job.state != ActivityState::running)
        return kLine;
    return job.detail.empty() ? kBarRow : kRow;
}

} // namespace

std::string transfer_speed(std::uint64_t done, std::uint64_t total, double seconds)
{
    if (seconds < kFirstSeconds || done == 0)
        return {};
    const double per_second = static_cast<double>(done) / seconds;
    char speed[32];
    std::snprintf(speed, sizeof(speed), "%.1f", per_second / (1024.0 * 1024.0));
    std::string text = i18n::trf("{0} MB/s", {speed});
    if (total <= done)
        return text;
    const auto minutes =
        static_cast<long long>(std::ceil(static_cast<double>(total - done) / per_second / 60.0));
    text += "  ·  ";
    if (minutes <= 1)
        return text + i18n::tr("about a minute left");
    if (minutes < 60)
        return text + i18n::trf("about {0} min left", {std::to_string(minutes)});
    return text + i18n::trf("about {0} h {1} min left",
                            {std::to_string(minutes / 60), std::to_string(minutes % 60)});
}

ActivityPanel::ActivityPanel()
{
    sheet_.style.edge = ui::SheetEdge::center;
    sheet_.style.size = 1280.0f;
    sheet_.style.height = 720.0f;
    sheet_.style.padding = 48.0f;
    sheet_.style.title_size = 42.0f;
    sheet_.set_title(i18n::tr("Activity"));
    sheet_.content = [this](ui::Canvas &canvas, const gfx::Rect &area, float)
    { draw_content(canvas, area); };
}

void ActivityPanel::set_theme(const ui::Theme &theme, bool reduced_motion)
{
    sheet_.style.theme = theme;
    sheet_.style.reduced_motion = reduced_motion;
    reduced_motion_ = reduced_motion;
}

void ActivityPanel::open(ui::Feedback &feedback)
{
    sheet_.set_title(i18n::tr("Activity")); // in the language chosen now
    sheet_.open(feedback);
}

void ActivityPanel::close(ui::Feedback &feedback)
{
    sheet_.close(feedback);
}

bool ActivityPanel::has_finished() const
{
    return std::any_of(
        jobs_.begin(), jobs_.end(), [](const ActivityJob &job)
        { return job.state == ActivityState::done || job.state == ActivityState::failed; });
}

int ActivityPanel::last_first() const
{
    float below = 0.0f;
    int first = static_cast<int>(jobs_.size());
    while (first > 0 &&
           below + row_height(jobs_[static_cast<std::size_t>(first - 1)]) <= rows_height_ + 0.5f)
    {
        --first;
        below += row_height(jobs_[static_cast<std::size_t>(first)]);
    }
    return first;
}

bool ActivityPanel::handle(const InputFrame &input, ui::Feedback &feedback)
{
    if (!sheet_.is_open())
        return false;
    if (input.is_pressed(Action::l3))
        return true;
    if (input.is_pressed(Action::west))
    {
        if (!has_finished())
            ui::refuse(feedback, sheet_.style, input, pulse_);
        else
        {
            clear_asked_ = true;
            ui::play_cue(feedback, sheet_.style, audio::Cue::erase);
        }
        return false;
    }
    if (input.nav == Direction::up || input.nav == Direction::down)
    {
        const int to = std::clamp(first_ + (input.nav == Direction::down ? 1 : -1), 0,
                                  std::max(0, last_first()));
        if (to == first_)
            ui::refuse(feedback, sheet_.style, input, pulse_);
        else
        {
            first_ = to;
            ui::play_cue(feedback, sheet_.style, audio::Cue::focus);
        }
        return false;
    }
    return sheet_.handle(input, feedback) == ui::Event::cancelled;
}

void ActivityPanel::update(float dt)
{
    clock_ += dt;
    // Jobs end and finished ones are cleared: the top row stays in the list.
    first_ = std::clamp(first_, 0, std::max(0, last_first()));
    pulse_.update(dt, 9.0f);
    sheet_.update(dt);
}

void ActivityPanel::draw(ui::Canvas &canvas) const
{
    sheet_.draw(canvas);
}

void ActivityPanel::draw_content(ui::Canvas &canvas, const gfx::Rect &area) const
{
    const ui::Theme &theme = sheet_.style.theme;
    ui::Painter paint(canvas.list, canvas.fonts, theme, canvas.glass);
    if (jobs_.empty())
        paint.body(i18n::tr("Nothing is installing or downloading"), area.x, area.y + 40.0f, kText,
                   theme.text_muted);
    rows_height_ = area.h - kHintRoom;
    // A refused scroll nudges the rows, as a list does at its ends.
    const float nudge =
        reduced_motion_ ? 0.0f : std::sin(pulse_.value * 18.0f) * 8.0f * pulse_.value;
    const float bottom = area.y + rows_height_ + 0.5f;
    float top = area.y;
    int last = first_ - 1;
    for (int i = first_; i < static_cast<int>(jobs_.size()); ++i)
    {
        const ActivityJob &job = jobs_[static_cast<std::size_t>(i)];
        const float height = row_height(job);
        if (top + height > bottom)
            break;
        last = i;
        const float y = top + nudge;
        top += height;
        if (job.state != ActivityState::running)
        {
            // A line: the name, and at the right what became of it.
            const bool waiting = job.state == ActivityState::waiting;
            const std::string word = waiting                            ? i18n::tr("Waiting")
                                     : job.state == ActivityState::done ? i18n::tr("Done")
                                                                        : i18n::tr("Failed");
            const gfx::Color tone = waiting                            ? theme.text_muted
                                    : job.state == ActivityState::done ? theme.accent
                                                                       : theme.text;
            const float word_w = paint.label_width(word, kDetail);
            paint.body(ui::fit_body(paint, job.text, kDetail, area.w - word_w - 32.0f), area.x,
                       y + 34.0f, kDetail, waiting ? theme.text_muted : theme.text);
            paint.label(word, area.x + area.w - word_w, y + 34.0f, kDetail, tone);
            continue;
        }
        const std::string text = ui::fit_body(paint, job.text, kText, area.w);
        paint.body(text, area.x, y + 34.0f, kText, theme.text);
        const gfx::Rect track{area.x, y + 54.0f, area.w, 10.0f};
        if (job.progress >= 0.0f)
            paint.progress(track, std::clamp(job.progress, 0.0f, 1.0f));
        else
        {
            // The end is not known: a stretch of the bar that sweeps across.
            canvas.list.rounded_rect(track, track.h * 0.5f, theme.text.with_alpha(0.12f));
            const float span = track.w * 0.25f;
            const float sweep = reduced_motion_ ? 0.375f : std::fmod(clock_ * 0.45f, 1.0f);
            const float x = track.x + (track.w - span) * sweep;
            canvas.list.rounded_rect({x, track.y, span, track.h}, track.h * 0.5f, theme.accent);
        }
        if (!job.detail.empty())
            paint.body(ui::fit_body(paint, job.detail, kDetail, area.w), area.x, y + 92.0f, kDetail,
                       theme.text_muted);
    }
    // The foot: which rows show when not all fit, Square to clear the
    // finished ones, Circle to close.
    const float size = 24.0f;
    const float cy = area.y + area.h - kHintSize * 0.5f;
    const int count = static_cast<int>(jobs_.size());
    if (count > 0 && (first_ > 0 || last < count - 1))
        paint.label(i18n::trf("{0}–{1} of {2}", {std::to_string(first_ + 1),
                                                 std::to_string(last + 1), std::to_string(count)}),
                    area.x, cy + size * 0.36f, size, theme.text_muted);
    float right = area.x + area.w;
    const auto hint = [&](Action action, const std::string &label)
    {
        const float label_w = paint.label_width(label, size);
        const float glyph_w = ui::action_glyph_width(action, kHintSize);
        ui::draw_action_glyph(canvas, theme, action, right - label_w - 12.0f - glyph_w, cy,
                              kHintSize);
        paint.label(label, right - label_w, cy + size * 0.36f, size, theme.text);
        right -= label_w + 12.0f + glyph_w + 36.0f;
    };
    hint(Action::back, i18n::tr("Close"));
    if (has_finished())
        hint(Action::west, i18n::tr("Clear finished"));
}

} // namespace hui::orion
