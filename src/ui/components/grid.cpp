// ORION Media Center - Component: GridView.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/grid.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

void GridView::set_items(std::vector<CardItem> items)
{
    items_ = std::move(items);
    sections_.clear();
    placed_columns_ = -1;
    virtual_count_ = -1;
    checks_.assign(items_.size(), 0.0f);
    for (std::size_t i = 0; i < items_.size(); ++i)
        checks_[i] = items_[i].selected ? 1.0f : 0.0f;
    focus_ = std::clamp(focus_, 0, std::max(count() - 1, 0));
    retarget(true);
}

void GridView::set_count(int count)
{
    count = std::max(count, 0);
    if (virtual_count_ == count)
        return;
    const bool first = virtual_count_ < 0;
    items_.clear();
    checks_.clear();
    placed_columns_ = -1;
    virtual_count_ = count;
    focus_ = std::clamp(focus_, 0, std::max(count - 1, 0));
    retarget(first);
}

void GridView::set_sections(std::vector<GridSection> sections)
{
    sections_ = std::move(sections);
    std::stable_sort(sections_.begin(), sections_.end(),
                     [](const GridSection &a, const GridSection &b) { return a.first < b.first; });
    placed_columns_ = -1;
    column_ = column_of(focus_);
    retarget(true);
}

void GridView::place() const
{
    if (placed_columns_ == columns())
        return;
    placed_columns_ = columns();
    row_starts_.clear();
    row_sections_.clear();
    row_headers_.clear();
    const int count = this->count();
    std::size_t next = 0;
    int headers = 0;
    for (int index = 0; index < count;)
    {
        // Sections that begin past the end, or on the same cell, collapse.
        int opens = -1;
        while (next < sections_.size() && sections_[next].first <= index)
            opens = static_cast<int>(next++);
        const int section_end =
            next < sections_.size() ? std::min(sections_[next].first, count) : count;
        if (opens >= 0)
            ++headers;
        row_starts_.push_back(index);
        row_sections_.push_back(opens);
        row_headers_.push_back(headers);
        index = std::min(index + placed_columns_, section_end);
    }
    row_starts_.push_back(count);
}

int GridView::row_of(int index) const
{
    if (sections_.empty())
        return index / columns();
    place();
    const auto after = std::upper_bound(row_starts_.begin(), row_starts_.end() - 1, index);
    return std::max(static_cast<int>(after - row_starts_.begin()) - 1, 0);
}

int GridView::column_of(int index) const
{
    return sections_.empty() ? index % columns() : index - row_start(row_of(index));
}

int GridView::row_start(int row) const
{
    if (sections_.empty())
        return row * columns();
    place();
    return row_starts_[static_cast<std::size_t>(std::clamp(row, 0, rows()))];
}

int GridView::row_length(int row) const
{
    if (sections_.empty())
        return std::clamp(count() - row * columns(), 0, columns());
    return row_start(row + 1) - row_start(row);
}

bool GridView::row_opens_section(int row) const
{
    if (sections_.empty() || row < 0 || row >= rows())
        return false;
    place();
    return row_sections_[static_cast<std::size_t>(row)] >= 0;
}

float GridView::row_y(const Layout &at, int row) const
{
    float y = static_cast<float>(row) * at.pitch;
    if (!sections_.empty() && row >= 0 && row < rows())
    {
        place();
        y += static_cast<float>(row_headers_[static_cast<std::size_t>(row)]) * style.section_gap;
    }
    return y;
}

float GridView::scroll_for(const Layout &at, int top) const
{
    // A row that opens a section brings its label into view with it.
    if (top <= 0)
        return 0.0f;
    return row_y(at, top) - (row_opens_section(top) ? style.section_gap : 0.0f);
}

float GridView::content_height(const Layout &at) const
{
    return at.rows > 0 ? row_y(at, at.rows - 1) + at.cell_h : 0.0f;
}

const CardItem &GridView::item_at(int index) const
{
    return virtual_count_ >= 0 ? blank_ : items_[static_cast<std::size_t>(index)];
}

Color GridView::accent_of(int index) const
{
    Color lit = accent ? accent(index) : item_at(index).accent;
    return lit.a > 0.0f ? lit : style.theme.focus;
}

void GridView::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    retarget(true);
}

void GridView::set_focus(int index, bool snap)
{
    if (count() == 0)
        return;
    focus_ = std::clamp(index, 0, count() - 1);
    column_ = column_of(focus_);
    retarget(snap);
}

void GridView::set_focus_at_top(int index)
{
    if (count() == 0)
        return;
    focus_ = std::clamp(index, 0, count() - 1);
    column_ = column_of(focus_);
    top_row_ = row_of(focus_); // retarget keeps it in the range the list allows
    retarget(true);
}

void GridView::enter()
{
    age_ = 0.0f;
}

int GridView::columns() const
{
    return std::max(style.columns, 1);
}

int GridView::rows() const
{
    if (sections_.empty())
        return (count() + columns() - 1) / columns();
    place();
    return static_cast<int>(row_starts_.size()) - 1;
}

GridView::Layout GridView::layout() const
{
    Layout at;
    at.columns = columns();
    at.rows = rows();
    const float across = static_cast<float>(at.columns);
    const float gaps = (across - 1.0f) * style.gap_x;
    const float grow = std::max(style.card.focus_scale - 1.0f, 0.0f);
    float pad_x = style.padding;
    if (style.padding >= 0.0f)
    {
        at.cell_w = style.cell_width > 0.0f ? style.cell_width
                                            : (bounds_.w - 2.0f * style.padding - gaps) / across;
        at.pad_y = style.padding;
    }
    else
    {
        // The focused cell grows by `grow` of its own width and wears a ring:
        // both must fit beside the outer columns. With automatic cells the
        // width depends on the padding and the padding on the width; solved.
        const float reach = card_ring_reach(style.theme) + 3.0f;
        at.cell_w = style.cell_width > 0.0f ? style.cell_width
                                            : (bounds_.w - 2.0f * reach - gaps) / (across + grow);
        pad_x = reach + at.cell_w * grow * 0.5f;
    }
    at.cell_w = std::max(at.cell_w, 8.0f);
    at.cell_h = style.cell_height > 0.0f ? style.cell_height : card_height(style.card, at.cell_w);
    at.cell_h = std::max(at.cell_h, 8.0f);
    if (style.padding < 0.0f)
        at.pad_y = card_ring_reach(style.theme) + 3.0f + at.cell_h * grow * 0.5f + style.card.lift;
    at.view = {bounds_.x + pad_x, bounds_.y + at.pad_y, across * at.cell_w + gaps,
               std::max(bounds_.h - 2.0f * at.pad_y, 1.0f)};
    at.pitch = at.cell_h + style.gap_y;
    at.full_rows = std::max(1, static_cast<int>((at.view.h + style.gap_y + 0.5f) / at.pitch));
    return at;
}

Rect GridView::content_rect(const Layout &at, int index) const
{
    return {static_cast<float>(column_of(index)) * (at.cell_w + style.gap_x),
            row_y(at, row_of(index)), at.cell_w, at.cell_h};
}

Rect GridView::cell_rect(int index) const
{
    const Layout at = layout();
    const Rect r = content_rect(at, index);
    return {at.view.x + r.x, at.view.y + r.y - scroll_.value, r.w, r.h};
}

// Where a direction leads from the focus, or -1 at an edge. `round` allows
// the moves that go round the grid.
int GridView::step(const Layout &at, Direction direction, bool round) const
{
    const int count = this->count();
    const int across = at.columns;
    const int row = row_of(focus_);
    const int column = column_of(focus_);
    const int last_row = at.rows - 1;
    const int in_row = row_length(row);
    const int wanted = std::min(column_, across - 1);
    // A short row (the last one, or a section's) takes its last cell; the
    // column is remembered.
    const auto in = [&](int r) { return row_start(r) + std::min(wanted, row_length(r) - 1); };
    round = round && style.wrap != GridWrap::none;
    switch (direction)
    {
    case Direction::left:
        if (column > 0)
            return focus_ - 1;
        if (style.wrap == GridWrap::flow && focus_ > 0)
            return focus_ - 1;
        if (!round)
            return -1;
        return style.wrap == GridWrap::rows ? row_start(row) + in_row - 1 : count - 1;
    case Direction::right:
        if (column + 1 < in_row)
            return focus_ + 1;
        if (style.wrap == GridWrap::flow && focus_ + 1 < count)
            return focus_ + 1;
        if (!round)
            return -1;
        return style.wrap == GridWrap::rows ? row_start(row) : 0;
    case Direction::up:
        if (row > 0)
            return in(row - 1);
        return round ? in(last_row) : -1;
    case Direction::down:
        if (row < last_row)
            return in(row + 1);
        return round ? in(0) : -1;
    default:
        return -1;
    }
}

void GridView::retarget(bool snap)
{
    if (count() == 0)
        return;
    const Layout at = layout();
    focus_ = std::clamp(focus_, 0, count() - 1);
    const Rect target = content_rect(at, focus_);
    // The grid scrolls by whole rows, the least that brings the focused row
    // into view: no row is ever cut at the top, and whatever does not fit at
    // the bottom peeks in, faded, to say there is more.
    const int row = row_of(focus_);
    if (sections_.empty())
    {
        top_row_ = std::clamp(top_row_, row - at.full_rows + 1, row);
        top_row_ = std::clamp(top_row_, 0, std::max(at.rows - at.full_rows, 0));
    }
    else
    {
        // Uneven rows: the same rule, measured. Never past the top that
        // shows the last row whole, never below the focused row, and as
        // little as brings the focused row in.
        const auto fits = [&](int top, int r)
        { return row_y(at, r) + at.cell_h - scroll_for(at, top) <= at.view.h + 0.5f; };
        int lowest = 0;
        while (lowest < at.rows - 1 && !fits(lowest, at.rows - 1))
            ++lowest;
        top_row_ = std::clamp(top_row_, 0, row);
        while (top_row_ < row && !fits(top_row_, row))
            ++top_row_;
        top_row_ = std::min(top_row_, lowest);
    }
    scroll_.target = scroll_for(at, top_row_);
    highlight_.target(target);
    const Color lit = accent_of(focus_);
    glow_.target(lit);
    if (snap)
    {
        scroll_.snap(scroll_.target);
        highlight_.snap(target);
        glow_.snap(lit);
    }
}

Event GridView::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    if (count() == 0)
        return Event::none;
    const Layout at = layout();
    if (input.nav != Direction::none)
    {
        // An edge that is an exit hands the focus over instead of wrapping,
        // and going round is a decision, not something a held direction does.
        const bool leaves = style.exits.allows(input.nav);
        const int next = step(at, input.nav, !leaves && !input.nav_repeat);
        if (next < 0 || next == focus_)
        {
            if (leaves)
            {
                exit_ = input.nav;
                return Event::none;
            }
            refused_ = input.nav;
            return refuse(feedback, style, input, refusal_, cell_rect(focus_).cx());
        }
        focus_ = next;
        if (input.nav == Direction::left || input.nav == Direction::right)
            column_ = column_of(focus_);
        retarget(false);
        const float along =
            at.rows > 1 ? static_cast<float>(row_of(focus_)) / static_cast<float>(at.rows - 1)
                        : 0.0f;
        play_cue(feedback, style, style.sounds.move, at.view.x + content_rect(at, focus_).cx(),
                 style.pitch_by_row ? tween::lerp(1.05f, 0.95f, along) : 1.0f);
        return Event::moved;
    }
    const float x = cell_rect(focus_).cx();
    if (input.is_pressed(Action::confirm))
    {
        if (virtual_count_ < 0 && items_[static_cast<std::size_t>(focus_)].disabled)
        {
            refused_ = Direction::none;
            return refuse(feedback, style, input, refusal_, x);
        }
        press_.trigger();
        if (virtual_count_ < 0 && style.select_on_confirm)
        {
            CardItem &item = items_[static_cast<std::size_t>(focus_)];
            item.selected = !item.selected;
            play_cue(feedback, style, style.sounds.change, x, item.selected ? 1.06f : 0.94f);
            return Event::changed;
        }
        play_cue(feedback, style, style.sounds.activate, x);
        if (style.sounds.rumble > 0.0f)
            feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
        return Event::activated;
    }
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

void GridView::update(float dt)
{
    age_ += dt;
    // Every frame, so a change of style (columns, sizes, theme) simply takes
    // effect: the focus and the scroll glide to where they now belong.
    retarget(false);
    highlight_.update(dt, style);
    scroll_.update(dt, std::max(style.omega(), 14.0f));
    glow_.update(dt, 8.0f);
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    press_.update(dt, 10.0f);
    refusal_.update(dt, 9.0f);
    const float ease = 1.0f - std::exp(-(style.reduced_motion ? 40.0f : 14.0f) * dt);
    for (std::size_t i = 0; i < items_.size() && i < checks_.size(); ++i)
    {
        const float target = items_[i].selected ? 1.0f : 0.0f;
        checks_[i] += (target - checks_[i]) * ease;
        if (std::fabs(target - checks_[i]) < 0.002f)
            checks_[i] = target;
    }
}

void GridView::draw(Canvas &canvas) const
{
    if (count() == 0)
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const Layout at = layout();
    const float scroll = scroll_.value;
    const bool overflow =
        sections_.empty() ? at.rows > at.full_rows : content_height(at) > at.view.h + 0.5f;
    const float active = active_amount_.value;

    // Sideways the clip is wide enough for shadows and rings; up and down it
    // is the bounds themselves once rows scroll through them.
    constexpr float kBleed = 28.0f;
    list.push_clip({bounds_.x - kBleed, bounds_.y - (overflow ? 0.0f : kBleed),
                    bounds_.w + 2.0f * kBleed, bounds_.h + (overflow ? 0.0f : 2.0f * kBleed)});

    const auto on_screen = [&](const Rect &c) -> Rect
    { return {at.view.x + c.x, at.view.y + c.y - scroll, c.w, c.h}; };
    // How visible a cell is: 1 inside the view, fading as its row is cut.
    const auto visibility = [&](const Rect &r)
    {
        if (!overflow || style.edge_fade <= 0.0f)
            return 1.0f;
        // A row that has scrolled out above is gone entirely, not left as a
        // sliver in the padding.
        const float top = std::max(bounds_.y, at.view.y - style.gap_y);
        const float shown = std::min(r.y + r.h - top, bounds_.y + bounds_.h - r.y);
        // Eased both ways: a sliver is all but gone, half a row is plain to see.
        return tween::smoothstep(shown / (r.h * style.edge_fade));
    };
    const auto entrance = [&](int index)
    {
        if (style.entrance_step <= 0.0f || style.reduced_motion)
            return tween::cubic_out(age_ / 0.2f);
        // Counted from the first row in view: a list entered far down (a
        // jump of thousands of rows) arrives as quickly as one at its top.
        const int row = std::max(row_of(index) - top_row_, 0);
        return tween::stagger(age_, row + column_of(index), style.entrance_step, 0.34f);
    };

    // A refusal nudges the focused cell and its ring along the refused axis.
    const float nudge = shake(refusal_.value, canvas.time, 9.0f);
    const bool vertical = refused_ == Direction::up || refused_ == Direction::down;
    const float nudge_x = vertical ? 0.0f : nudge;
    const float nudge_y = vertical ? nudge : 0.0f;

    const auto draw_cell = [&](int index, float focus)
    {
        const CardItem &item = item_at(index);
        const Rect r = on_screen(content_rect(at, index));
        const float arrived = entrance(index);
        const float alpha = visibility(r) * arrived;
        if (alpha <= 0.0f)
            return;
        const bool held = focus > 0.5f;
        CardState state;
        state.focus = focus;
        state.press = held ? press_.value : 0.0f;
        state.selected = virtual_count_ < 0 ? checks_[static_cast<std::size_t>(index)] : 0.0f;
        state.marks = false; // one ring glides for the whole grid
        list.push_opacity(alpha);
        list.push_transform(1.0f, 0.0f, 0.0f, held ? nudge_x : 0.0f,
                            (style.reduced_motion ? 0.0f : 20.0f * (1.0f - arrived)) +
                                (held ? nudge_y : 0.0f));
        if (content)
        {
            list.push_transform(card_scale(style, style.card, state), r.cx(), r.cy(), 0.0f,
                                -card_lift(style, style.card, state));
            content(canvas, r, item, index, focus);
            list.pop_transform();
        }
        else
        {
            draw_card(canvas, style, style.card, r, item, state, art);
        }
        list.pop_transform();
        list.pop_opacity();
    };

    // Cells the highlight touches are drawn last, so the one that grows lies
    // over its neighbours. While it glides that is up to four cells.
    constexpr int kHot = 6;
    int hot[kHot];
    float hot_focus[kHot];
    int hot_count = 0;
    int first_row = std::clamp(static_cast<int>(std::floor((scroll - at.pad_y) / at.pitch)), 0,
                               std::max(at.rows - 1, 0));
    int last_row =
        std::clamp(static_cast<int>(std::floor((scroll + bounds_.h) / at.pitch)), 0, at.rows - 1);
    if (!sections_.empty())
    {
        // Labels push rows down: walk to the ones in view.
        first_row = 0;
        while (first_row < at.rows - 1 && row_y(at, first_row + 1) <= scroll - at.pad_y)
            ++first_row;
        last_row = first_row;
        while (last_row < at.rows - 1 && row_y(at, last_row + 1) < scroll + bounds_.h)
            ++last_row;
        const Painter paint(list, canvas.fonts, theme, canvas.glass);
        const Color ink = style.card.on_panel ? theme.text_muted : paint.page_text_muted();
        const FontRef &font = canvas.fonts.semibold;
        for (int row = first_row; row <= last_row; ++row)
        {
            const int opens = row_sections_[static_cast<std::size_t>(row)];
            if (opens < 0)
                continue;
            const std::string &label = sections_[static_cast<std::size_t>(opens)].label;
            const Rect band =
                on_screen({0.0f, row_y(at, row) - style.section_gap, at.view.w, style.section_gap});
            const float alpha = visibility(band) * entrance(row_start(row));
            if (alpha <= 0.0f)
                continue;
            // Clear of the focused card's lift and ring, which grow upwards.
            const float baseline = band.y + band.h * 0.5f + style.section_text * 0.1f;
            list.push_opacity(alpha);
            text(list, font, label.c_str(), band.x, baseline, style.section_text, ink);
            if (style.section_rule)
            {
                const float from = band.x + font.font->measure(label, style.section_text) + 18.0f;
                if (from < band.x + band.w)
                    list.rounded_rect(
                        {from, baseline - style.section_text * 0.32f, band.x + band.w - from, 1.0f},
                        0.0f, ink.with_alpha(0.3f));
            }
            list.pop_opacity();
        }
    }
    const int count = this->count();
    for (int index = row_start(first_row); index < std::min(row_start(last_row + 1), count);
         ++index)
    {
        const float focus = highlight_.coverage(content_rect(at, index)) * active;
        if (focus > 0.001f && hot_count < kHot)
        {
            // Insertion keeps them in rising order of focus.
            int slot = hot_count++;
            while (slot > 0 && hot_focus[slot - 1] > focus)
            {
                hot[slot] = hot[slot - 1];
                hot_focus[slot] = hot_focus[slot - 1];
                --slot;
            }
            hot[slot] = index;
            hot_focus[slot] = focus;
        }
        else
        {
            draw_cell(index, 0.0f);
        }
    }

    // The one focus indicator: where the highlight is, as large as a card
    // under it is drawn.
    Rect under = on_screen(highlight_.rect(0.0f));
    under.x += nudge_x;
    under.y += nudge_y;
    CardState lead;
    lead.focus = active;
    lead.press = press_.value;
    const Rect frame =
        card_placed(under, content ? under : card_frame(style.card, under),
                    card_scale(style, style.card, lead), card_lift(style, style.card, lead));
    const float amount = (0.35f + 0.65f * active) * entrance(focus_) * visibility(under);
    draw_card_halo(canvas, style, style.card, frame, glow_.value(), amount * active);
    for (int i = 0; i < hot_count; ++i)
        draw_cell(hot[i], hot_focus[i]);
    draw_card_ring(canvas, style, style.card, frame, amount);
    list.pop_clip();

    if (style.scroll_thumb && overflow)
    {
        const Painter paint(list, canvas.fonts, theme, canvas.glass);
        const Color ink = style.card.on_panel ? theme.text_muted : paint.page_text_muted();
        const float track = bounds_.h - 16.0f;
        const float total = sections_.empty() ? static_cast<float>(at.rows) * at.pitch
                                              : content_height(at) + style.gap_y;
        const float size = std::max(track * std::min(at.view.h / total, 1.0f), 36.0f);
        const float travel = track - size;
        float reach = static_cast<float>(at.rows - at.full_rows) * at.pitch;
        if (!sections_.empty())
        {
            int lowest = 0;
            while (lowest < at.rows - 1 &&
                   content_height(at) - scroll_for(at, lowest) > at.view.h + 0.5f)
                ++lowest;
            reach = scroll_for(at, lowest);
        }
        const float along = reach > 0.0f ? scroll / reach : 0.0f;
        const float x = bounds_.x + bounds_.w + 8.0f;
        const float round = theme.corner == Corner::round && theme.radius >= 2.0f ? 2.0f : 0.0f;
        list.rounded_rect({x, bounds_.y + 8.0f, 4.0f, track}, round, ink.with_alpha(0.16f));
        list.rounded_rect({x, bounds_.y + 8.0f + travel * tween::clamp01(along), 4.0f, size}, round,
                          ink.with_alpha(0.7f));
    }
}

} // namespace hui::ui
