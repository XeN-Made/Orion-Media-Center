// ORION Media Center - The font set every screen draws with.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/draw_list.hpp"
#include "gfx/font.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hui::ui
{

// A loaded font and the GL texture holding its atlas.
struct FontRef
{
    const gfx::Font *font = nullptr;
    std::uint32_t texture = 0;

    float measure(std::string_view text, float size, float tracking = 0.0f) const
    {
        return font->measure(text, size, tracking);
    }
};

struct Fonts
{
    FontRef regular;  // Inter Regular: body text
    FontRef semibold; // Inter SemiBold: titles, labels, buttons
    FontRef display;  // Montserrat Medium: wide geometric headlines
    FontRef mono;     // DejaVu Sans Mono: numbers that must not jump, terminals
    FontRef pixel;    // Press Start 2P: an 8x8 bitmap face (the Pixel theme)
    FontRef hand;     // Patrick Hand: handwriting (the Sketch theme)
};

// Draws one line with its baseline at y; x is the left edge, centre or right
// edge depending on align. Returns the width.
inline float text(gfx::DrawList &list, const FontRef &font, std::string_view value, float x,
                  float baseline, float size, gfx::Color color, gfx::Align align = gfx::Align::left,
                  float tracking = 0.0f)
{
    return list.text(*font.font, font.texture, value, x, baseline, size, color, align, tracking);
}

// The capital of a lower-case letter the baked faces carry (Latin-1, Latin
// Extended-A, Greek, Cyrillic), else the letter itself. Latin Extended-A
// pairs capital and small as even and odd code points, except where a run
// starts on an odd one (Ĺ ... ň, Ź ... ž).
constexpr char32_t upper_letter(char32_t c)
{
    if ((c >= U'a' && c <= U'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7) ||
        (c >= 0x3B1 && c <= 0x3CB && c != 0x3C2) || (c >= 0x430 && c <= 0x44F))
        return c - 0x20;
    if (c == 0xFF)
        return 0x178; // ÿ
    if (c == 0x3C2)
        return 0x3A3; // final sigma
    if (c >= 0x450 && c <= 0x45F)
        return c - 0x50;
    const bool odd_run = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
    if (odd_run)
        return c % 2 == 0 ? c - 1 : c;
    if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177))
        return c % 2 == 1 ? c - 1 : c;
    return c;
}

// Upper case, for small tracked labels ("CONTINUE PLAYING", "KÖNYVTÁR"): the
// letters above; anything else, and text that is not UTF-8, is kept as it is.
inline std::string upper(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size();)
    {
        const auto byte = static_cast<unsigned char>(value[i]);
        // Two-byte sequences hold every letter upper_letter changes beyond ASCII.
        if (byte >= 0xC2 && byte <= 0xDF && i + 1 < value.size() &&
            (static_cast<unsigned char>(value[i + 1]) & 0xC0) == 0x80)
        {
            const char32_t c = (static_cast<char32_t>(byte & 0x1F) << 6) |
                               (static_cast<unsigned char>(value[i + 1]) & 0x3F);
            const char32_t up = upper_letter(c);
            result += static_cast<char>(0xC0 | (up >> 6));
            result += static_cast<char>(0x80 | (up & 0x3F));
            i += 2;
            continue;
        }
        result += byte < 0x80 ? static_cast<char>(upper_letter(byte)) : value[i];
        ++i;
    }
    return result;
}

// A paragraph as it is drawn: word-wrapped, at most max_lines lines (never
// fewer than one), the last one ending in an ellipsis if text remains. Empty
// text is one empty line. This is the one place that decides it, so what is
// measured and what is drawn cannot disagree.
struct ParagraphLines
{
    std::vector<std::string> lines;
    bool truncated = false; // text remained after max_lines: the last line shows the ellipsis
};

inline ParagraphLines paragraph_lines(const FontRef &font, std::string_view value, float size,
                                      float width, int max_lines = 99)
{
    ParagraphLines result;
    result.lines = font.font->wrap(value, size, width);
    const std::size_t cap = static_cast<std::size_t>(std::max(max_lines, 1));
    if (result.lines.size() > cap)
    {
        result.lines.resize(cap);
        result.lines.back() = font.font->fit(result.lines.back() + " \xE2\x80\xA6", size, width);
        result.truncated = true;
    }
    return result;
}

// Draws lines from paragraph_lines() from the first baseline at y. Returns
// the baseline after the last line drawn.
inline float paragraph(gfx::DrawList &list, const FontRef &font, const ParagraphLines &lines,
                       float x, float y, float size, float line_height, gfx::Color color,
                       gfx::Align align = gfx::Align::left)
{
    for (const std::string &line : lines.lines)
    {
        text(list, font, line, x, y, size, color, align);
        y += line_height;
    }
    return y;
}

// Draws word-wrapped text from its first baseline at y, at most max_lines
// lines (the last one ends in an ellipsis if text remains). Returns the
// baseline after the last line drawn.
inline float paragraph(gfx::DrawList &list, const FontRef &font, std::string_view value, float x,
                       float y, float size, float width, float line_height, gfx::Color color,
                       int max_lines = 99, gfx::Align align = gfx::Align::left)
{
    return paragraph(list, font, paragraph_lines(font, value, size, width, max_lines), x, y, size,
                     line_height, color, align);
}

// The space paragraph() will take, measured without drawing. Lay out what
// follows a block of text, or work out how much of a list fits, before
// drawing any of it.
struct ParagraphMetrics
{
    int lines = 0;          // lines paragraph() draws
    float height = 0.0f;    // lines * line_height: how far paragraph() moves y
    float width = 0.0f;     // widest line as drawn, ellipsis included
    bool truncated = false; // text remained after max_lines: the last line shows the ellipsis
};

// Wrapping allocates and measures every word: a screen that measures and then
// draws the same text wraps once with paragraph_lines() and passes the result
// to both.
inline ParagraphMetrics measure_paragraph(const FontRef &font, const ParagraphLines &lines,
                                          float size, float line_height)
{
    ParagraphMetrics m;
    m.lines = static_cast<int>(lines.lines.size());
    m.height = static_cast<float>(m.lines) * line_height;
    for (const std::string &line : lines.lines)
        m.width = std::max(m.width, font.measure(line, size));
    m.truncated = lines.truncated;
    return m;
}

inline ParagraphMetrics measure_paragraph(const FontRef &font, std::string_view value, float size,
                                          float width, float line_height, int max_lines = 99)
{
    return measure_paragraph(font, paragraph_lines(font, value, size, width, max_lines), size,
                             line_height);
}

} // namespace hui::ui
