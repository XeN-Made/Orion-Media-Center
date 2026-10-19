// ps5-homebrew-ui - ORION's look: a base theme plus the player's own choices.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/backdrop_spec.hpp"
#include "gfx/draw_list.hpp"
#include "ui/theme.hpp"

#include <span>
#include <string>
#include <string_view>

namespace hui::orion
{

// What Settings > Appearance edits, kept in <state_dir>/theme.json:
//
//   {
//     "base": "orion",
//     "background": "galaxy",
//     "sky_top": "#000000", "sky_bottom": "#02040b",
//     "nebula": "#16285e", "nebula_far": "#0a4a66",
//     "clouds": 100, "motion": true,
//     "glass": "#0d1024", "glass_opacity": 40,
//     "highlight": "#a9b8ff", "accent": "#8fe3ff",
//     "corners": 22
//   }
//
// Every key is optional: a missing one keeps the base theme's own value, so
// the file can be as short as {"base": "acrylic"}.
struct Look
{
    std::string base = "orion"; // a ui::Theme id: the starting point
    gfx::BackdropMode background = gfx::BackdropMode::stars;
    gfx::Color sky_top;    // backdrop colour 0
    gfx::Color sky_bottom; // backdrop colour 1
    gfx::Color nebula;     // backdrop colour 2 (and the nearer clouds)
    gfx::Color nebula_far; // backdrop colour 3 (and the farther clouds)
    float clouds = 1.0f;   // 0..2: how strong the drifting clouds are; 1 as designed
    bool motion = true;    // the sky drifts; off holds it still
    gfx::Color glass;      // tint of frosted surfaces; its alpha is the opacity
    gfx::Color highlight;  // the selected tab, the focused key
    gfx::Color accent;     // labels, counts, the caret
    float corners = 22.0f; // radius of the covers
};

// The base theme with this id, or ORION's when there is none.
const ui::Theme &base_theme(std::string_view id);
// A base theme's own look: what choosing it in Settings starts from.
Look look_of(const ui::Theme &theme);
// ORION's look, as designed.
Look default_look();
// The base theme with the look applied: what the screens draw with.
ui::Theme themed(const Look &look);

Look load_look(const std::string &state_dir);
bool save_look(const std::string &state_dir, const Look &look);

// ---- the choices Settings offers ----

struct Background
{
    const char *id;   // in theme.json
    const char *name; // in Settings
    gfx::BackdropMode mode;
};
std::span<const Background> backgrounds();
const char *background_id(gfx::BackdropMode mode);

// A named colour, or a named pair (sky and nebula are pairs).
struct Swatch
{
    const char *name;
    std::uint32_t first;
    std::uint32_t second;
};
std::span<const Swatch> skies();
std::span<const Swatch> nebulas();
std::span<const Swatch> glasses();
std::span<const Swatch> highlights();
std::span<const Swatch> accents();

// "#rrggbb" (alpha is not written); parsing also takes "rrggbb".
std::string to_hex(gfx::Color color);
bool from_hex(std::string_view text, gfx::Color *color);
// Same colour, ignoring alpha, to within rounding.
bool same_rgb(gfx::Color a, gfx::Color b);

} // namespace hui::orion
