// ORION Media Center - What's new: the changes of the version just installed, shown once.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/whats_new.hpp"

#include "core/i18n.hpp"
#include "ui/components/action_glyph.hpp"

#include <cctype>

namespace hui::orion
{

namespace
{

std::string trimmed(std::string_view text)
{
    std::size_t start = 0;
    std::size_t end = text.size();
    while (start < end && std::isspace(static_cast<unsigned char>(text[start])) != 0)
        ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
        --end;
    return std::string(text.substr(start, end - start));
}

} // namespace

bool parse_whats_new(std::string_view text, WhatsNewNotes *notes)
{
    *notes = WhatsNewNotes();
    std::size_t start = 0;
    while (start < text.size())
    {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string line = trimmed(text.substr(start, end - start));
        start = end + 1;
        if (line.empty())
            continue;
        if (line.rfind("## ", 0) == 0)
        {
            ui::DetailItem section;
            section.label = trimmed(line.substr(3));
            section.header = true;
            notes->items.push_back(std::move(section));
            continue;
        }
        if (line.rfind("# ", 0) == 0)
        {
            if (notes->version.empty())
                notes->version = trimmed(line.substr(2));
            continue;
        }
        ui::DetailItem change;
        const std::size_t bar = line.find(" | ");
        if (bar == std::string::npos)
            change.value = line;
        else
        {
            change.label = trimmed(line.substr(0, bar));
            change.value = trimmed(line.substr(bar + 3));
        }
        notes->items.push_back(std::move(change));
    }
    return !notes->version.empty() && !notes->items.empty();
}

bool show_whats_new(const WhatsNewNotes &notes, const std::string &version, const std::string &seen,
                    bool set_up)
{
    return set_up && !notes.version.empty() && notes.version == version && seen != version;
}

namespace
{

constexpr float kHintSize = 34.0f;
constexpr float kHintRoom = 62.0f; // under the notes, for the hint

// Where the notes go: the sheet's content less a row for the hint.
gfx::Rect notes_rect(const gfx::Rect &content)
{
    return {content.x, content.y, content.w, std::max(content.h - kHintRoom, 0.0f)};
}

} // namespace

WhatsNew::WhatsNew()
{
    // Read once, start to end: big type in the middle of the screen, not a
    // drawer at its edge.
    sheet_.style.edge = ui::SheetEdge::center;
    sheet_.style.size = 1280.0f;
    sheet_.style.height = 860.0f;
    sheet_.style.padding = 48.0f;
    sheet_.style.title_size = 42.0f;
    sheet_.content = [this](ui::Canvas &canvas, const gfx::Rect &area, float)
    {
        rows_.draw(canvas);
        // Circle closes: the glyph and the word, at the panel's foot.
        const ui::Theme &theme = sheet_.style.theme;
        ui::Painter paint(canvas.list, canvas.fonts, theme, canvas.glass);
        const std::string label = i18n::tr("Close");
        const float text = 24.0f;
        const float label_w = paint.label_width(label, text);
        const float glyph_w = ui::action_glyph_width(Action::back, kHintSize);
        const float right = area.x + area.w;
        const float cy = area.y + area.h - kHintSize * 0.5f;
        ui::draw_action_glyph(canvas, theme, Action::back, right - label_w - 12.0f - glyph_w, cy,
                              kHintSize);
        paint.label(label, right - label_w, cy + text * 0.36f, text, theme.text);
    };
    rows_.style.panel = false;
    rows_.style.copy_mark = false;
    rows_.style.layout = ui::DetailLayout::stacked;
    rows_.style.label_size = 27.0f;
    rows_.style.value_size = 25.0f;
    rows_.style.header_size = 21.0f;
    rows_.style.header_height = 56.0f;
    rows_.style.padding_y = 12.0f;
    rows_.style.stack_gap = 6.0f;
    rows_.style.max_lines = 6;
}

void WhatsNew::set_theme(const ui::Theme &theme, bool reduced_motion)
{
    sheet_.style.theme = rows_.style.theme = theme;
    sheet_.style.reduced_motion = rows_.style.reduced_motion = reduced_motion;
}

void WhatsNew::open(const WhatsNewNotes &notes, ui::Feedback &feedback)
{
    sheet_.set_title(i18n::trf("What's new in ORION {0}", {notes.version}));
    rows_.set_items(notes.items);
    rows_.set_active(true);
    rows_.set_bounds(notes_rect(sheet_.content_rect()));
    rows_.enter();
    sheet_.open(feedback);
}

void WhatsNew::close(ui::Feedback &feedback)
{
    sheet_.close(feedback);
}

bool WhatsNew::handle(const InputFrame &input, ui::Feedback &feedback)
{
    if (!sheet_.is_open())
        return false;
    if (sheet_.handle(input, feedback) == ui::Event::cancelled)
        return true;
    // Up and down read the notes (nothing in them takes the focus).
    rows_.handle(input, feedback);
    return false;
}

void WhatsNew::update(float dt)
{
    rows_.set_bounds(notes_rect(sheet_.content_rect()));
    sheet_.update(dt);
    rows_.update(dt);
}

void WhatsNew::draw(ui::Canvas &canvas) const
{
    sheet_.draw(canvas);
}

} // namespace hui::orion
