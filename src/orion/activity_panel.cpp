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

namespace hui::orion
{

namespace
{

constexpr float kHintSize = 34.0f;
constexpr float kHintRoom = 62.0f; // under the jobs, for the hint
constexpr float kRow = 96.0f;
constexpr float kText = 26.0f;

} // namespace

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

bool ActivityPanel::handle(const InputFrame &input, ui::Feedback &feedback)
{
    if (!sheet_.is_open())
        return false;
    if (input.is_pressed(Action::l3))
        return true;
    return sheet_.handle(input, feedback) == ui::Event::cancelled;
}

void ActivityPanel::update(float dt)
{
    clock_ += dt;
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
    const int fit = std::max(1, static_cast<int>((area.h - kHintRoom) / kRow));
    for (int i = 0; i < static_cast<int>(jobs_.size()) && i < fit; ++i)
    {
        const ActivityJob &job = jobs_[static_cast<std::size_t>(i)];
        const float top = area.y + static_cast<float>(i) * kRow;
        const std::string text = ui::fit_body(paint, job.text, kText, area.w);
        paint.body(text, area.x, top + 34.0f, kText, theme.text);
        const gfx::Rect track{area.x, top + 54.0f, area.w, 10.0f};
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
    }
    // Circle closes: the glyph and the word, at the panel's foot.
    const std::string label = i18n::tr("Close");
    const float size = 24.0f;
    const float label_w = paint.label_width(label, size);
    const float glyph_w = ui::action_glyph_width(Action::back, kHintSize);
    const float right = area.x + area.w;
    const float cy = area.y + area.h - kHintSize * 0.5f;
    ui::draw_action_glyph(canvas, theme, Action::back, right - label_w - 12.0f - glyph_w, cy,
                          kHintSize);
    paint.label(label, right - label_w, cy + size * 0.36f, size, theme.text);
}

} // namespace hui::orion
