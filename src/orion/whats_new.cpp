// ORION Media Center - What's new: the changes of the version just installed, shown once.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/whats_new.hpp"

#include "core/i18n.hpp"

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

WhatsNew::WhatsNew()
{
    sheet_.style.edge = ui::SheetEdge::right;
    sheet_.style.size = 760.0f;
    sheet_.style.title_size = 34.0f;
    sheet_.content = [this](ui::Canvas &canvas, const gfx::Rect &, float) { rows_.draw(canvas); };
    rows_.style.panel = false;
    rows_.style.copy_mark = false;
    rows_.style.layout = ui::DetailLayout::stacked;
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
    rows_.set_bounds(sheet_.content_rect());
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
    rows_.set_bounds(sheet_.content_rect());
    sheet_.update(dt);
    rows_.update(dt);
}

void WhatsNew::draw(ui::Canvas &canvas) const
{
    sheet_.draw(canvas);
}

} // namespace hui::orion
