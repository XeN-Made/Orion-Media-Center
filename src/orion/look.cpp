// ps5-homebrew-ui - ORION's look: a base theme plus the player's own choices.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/look.hpp"

#include "core/save_file.hpp"
#include "library/json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace hui::orion
{

namespace
{

using gfx::BackdropMode;
using gfx::Color;

constexpr Background kBackgrounds[] = {
    {"galaxy", "Galaxy", BackdropMode::stars},
    {"aurora", "Aurora", BackdropMode::aurora},
    {"waves", "Waves", BackdropMode::waves},
    {"bokeh", "Bokeh", BackdropMode::bokeh},
    {"gradient", "Gradient", BackdropMode::gradient},
    {"synthwave", "Synthwave", BackdropMode::grid},
    {"dots", "Dots", BackdropMode::dots},
    {"vista", "Vista", BackdropMode::vista},
    {"paper", "Paper", BackdropMode::paper},
    {"phosphor", "Phosphor", BackdropMode::phosphor},
};

constexpr Swatch kSkies[] = {
    {"Deep space", 0x000000, 0x02040b}, {"Midnight", 0x02040f, 0x0a1430},
    {"Ink", 0x050505, 0x111114},        {"Teal night", 0x000a0c, 0x042026},
    {"Plum", 0x07020c, 0x180a26},       {"Ember", 0x0a0302, 0x1e0a06},
};
constexpr Swatch kNebulas[] = {
    {"Blue and teal", 0x16285e, 0x0a4a66}, {"Faint", 0x0a1026, 0x061a24},
    {"Violet", 0x34208c, 0x0c5a80},        {"Rose", 0x5a1846, 0x2a1250},
    {"Emerald", 0x0c4a3a, 0x0a3a5a},       {"Ember", 0x5a2a0a, 0x4a0a1a},
};
constexpr Swatch kGlasses[] = {
    {"Smoke", 0x0d1024, 0},    {"Clear", 0xffffff, 0}, {"Midnight", 0x060818, 0},
    {"Charcoal", 0x111111, 0}, {"Frost", 0xdfe8ff, 0},
};
constexpr Swatch kHighlights[] = {
    {"Lavender", 0xa9b8ff, 0}, {"Ice", 0x8fe3ff, 0},  {"White", 0xffffff, 0},
    {"Teal", 0x3fe0c5, 0},     {"Gold", 0xffc94a, 0}, {"Rose", 0xff7aa8, 0},
};
constexpr Swatch kAccents[] = {
    {"Ice", 0x8fe3ff, 0},  {"Teal", 0x3fe0c5, 0}, {"Lavender", 0xa9b8ff, 0}, {"Gold", 0xffc94a, 0},
    {"Rose", 0xff7aa8, 0}, {"Lime", 0xb6f36a, 0}, {"White", 0xffffff, 0},
};

std::string file_of(const std::string &state_dir)
{
    return state_dir + "/theme.json";
}

void read_colour(const json::Value &root, const char *key, Color *colour)
{
    Color parsed;
    if (from_hex(root[key].string_or(""), &parsed))
        *colour = parsed.with_alpha(colour->a);
}

// Black or white, whichever reads on the fill.
Color ink_on(Color fill)
{
    const float luma = 0.2126f * fill.r + 0.7152f * fill.g + 0.0722f * fill.b;
    return luma > 0.55f ? Color::rgb(0x0a0c16) : Color::rgb(0xffffff);
}

const ThemeFile &base_of(const Look &look, const ThemeLibrary &themes)
{
    const ThemeFile *file = themes.find(look.base);
    return file != nullptr ? *file : themes.fallback();
}

} // namespace

Look look_of(const ThemeFile &file)
{
    const ui::Theme &theme = file.theme;
    Look look;
    look.base = file.id;
    look.clouds = file.clouds;
    look.motion = file.motion;
    look.corners = file.cover_corners;
    look.background = theme.backdrop.mode;
    look.sky_top = theme.backdrop.colors[0];
    look.sky_bottom = theme.backdrop.colors[1];
    look.nebula = theme.backdrop.colors[2];
    look.nebula_far = theme.backdrop.colors[3];
    look.glass = theme.surface;
    look.highlight = theme.primary;
    look.accent = theme.accent;
    return look;
}

Look default_look(const ThemeLibrary &themes)
{
    return look_of(themes.fallback());
}

ui::Theme themed(const Look &look, const ThemeLibrary &themes)
{
    ui::Theme theme = base_of(look, themes).theme;
    theme.backdrop.mode = look.background;
    theme.backdrop.colors[0] = look.sky_top;
    theme.backdrop.colors[1] = look.sky_bottom;
    theme.backdrop.colors[2] = look.nebula;
    theme.backdrop.colors[3] = look.nebula_far;
    theme.surface = look.glass;
    theme.primary = look.highlight;
    theme.on_primary = ink_on(look.highlight);
    theme.accent = look.accent;
    return theme;
}

ThemeFile theme_of(const Look &look, const ThemeLibrary &themes)
{
    ThemeFile file = base_of(look, themes);
    file.theme = themed(look, themes);
    file.clouds = look.clouds;
    file.motion = look.motion;
    file.cover_corners = look.corners;
    file.path.clear();
    file.user = false;
    file.bind();
    return file;
}

Look load_look(const std::string &state_dir, const ThemeLibrary &themes)
{
    std::string text;
    json::Value root;
    if (state_dir.empty() || !save::read_file(file_of(state_dir), &text, 1u << 20) ||
        !json::parse(text, &root) || !root.is_object())
        return default_look(themes);

    // The base theme's own look first; every key present then overrides it.
    const ThemeFile *base = themes.find(root["base"].string_or("orion"));
    Look look = look_of(base != nullptr ? *base : themes.fallback());
    const std::string background = root["background"].string_or("");
    for (const Background &kind : kBackgrounds)
    {
        if (background == kind.id)
            look.background = kind.mode;
    }
    read_colour(root, "sky_top", &look.sky_top);
    read_colour(root, "sky_bottom", &look.sky_bottom);
    read_colour(root, "nebula", &look.nebula);
    read_colour(root, "nebula_far", &look.nebula_far);
    read_colour(root, "glass", &look.glass);
    read_colour(root, "highlight", &look.highlight);
    read_colour(root, "accent", &look.accent);
    const double opacity = root["glass_opacity"].number_or(-1.0);
    if (opacity >= 0.0)
        look.glass.a = std::clamp(static_cast<float>(opacity) / 100.0f, 0.0f, 1.0f);
    look.clouds = std::clamp(
        static_cast<float>(root["clouds"].number_or(look.clouds * 100.0) / 100.0), 0.0f, 2.0f);
    look.motion = root["motion"].bool_or(look.motion);
    look.corners =
        std::clamp(static_cast<float>(root["corners"].number_or(look.corners)), 0.0f, 48.0f);
    return look;
}

bool save_look(const std::string &state_dir, const Look &look)
{
    if (state_dir.empty() || !save::ensure_directory(state_dir))
        return false;
    char numbers[160];
    std::snprintf(numbers, sizeof(numbers),
                  "  \"clouds\": %d,\n  \"motion\": %s,\n  \"glass_opacity\": %d,\n"
                  "  \"corners\": %d\n",
                  static_cast<int>(std::lround(look.clouds * 100.0f)),
                  look.motion ? "true" : "false",
                  static_cast<int>(std::lround(look.glass.a * 100.0f)),
                  static_cast<int>(std::lround(look.corners)));
    std::string text = "{\n";
    text += "  \"base\": " + json::quote(look.base) + ",\n";
    text += "  \"background\": " + json::quote(background_id(look.background)) + ",\n";
    text += "  \"sky_top\": \"" + to_hex(look.sky_top) + "\",\n";
    text += "  \"sky_bottom\": \"" + to_hex(look.sky_bottom) + "\",\n";
    text += "  \"nebula\": \"" + to_hex(look.nebula) + "\",\n";
    text += "  \"nebula_far\": \"" + to_hex(look.nebula_far) + "\",\n";
    text += "  \"glass\": \"" + to_hex(look.glass) + "\",\n";
    text += "  \"highlight\": \"" + to_hex(look.highlight) + "\",\n";
    text += "  \"accent\": \"" + to_hex(look.accent) + "\",\n";
    text += numbers;
    text += "}\n";
    return save::write_atomic(file_of(state_dir), text).empty();
}

std::span<const Background> backgrounds()
{
    return kBackgrounds;
}

const char *background_id(BackdropMode mode)
{
    for (const Background &kind : kBackgrounds)
    {
        if (kind.mode == mode)
            return kind.id;
    }
    return "gradient";
}

std::span<const Swatch> skies()
{
    return kSkies;
}
std::span<const Swatch> nebulas()
{
    return kNebulas;
}
std::span<const Swatch> glasses()
{
    return kGlasses;
}
std::span<const Swatch> highlights()
{
    return kHighlights;
}
std::span<const Swatch> accents()
{
    return kAccents;
}

std::string to_hex(Color color)
{
    const auto byte = [](float v)
    { return static_cast<unsigned>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    char text[8];
    std::snprintf(text, sizeof(text), "#%02x%02x%02x", byte(color.r), byte(color.g), byte(color.b));
    return text;
}

bool from_hex(std::string_view text, Color *color)
{
    if (!text.empty() && text.front() == '#')
        text.remove_prefix(1);
    if (text.size() != 6)
        return false;
    std::uint32_t value = 0;
    for (char c : text)
    {
        value <<= 4;
        if (c >= '0' && c <= '9')
            value |= static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            value |= static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            value |= static_cast<std::uint32_t>(c - 'A' + 10);
        else
            return false;
    }
    *color = Color::rgb(value);
    return true;
}

bool same_rgb(Color a, Color b)
{
    const float tolerance = 1.5f / 255.0f;
    return std::fabs(a.r - b.r) < tolerance && std::fabs(a.g - b.g) < tolerance &&
           std::fabs(a.b - b.b) < tolerance;
}

} // namespace hui::orion
