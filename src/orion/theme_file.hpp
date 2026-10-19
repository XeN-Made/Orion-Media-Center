// ps5-homebrew-ui - ORION theme files: a whole look as one JSON document.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/theme.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hui::orion
{

// How ORION's own pieces are drawn, beyond what the kit's Theme says.
enum class TabStyle : std::uint8_t
{
    pills, // a frosted track with a plate behind the active tab
    plain, // text only, the active tab lit and underlined (the home screen's way)
};
enum class FocusRing : std::uint8_t
{
    theme,  // the theme's own focus ring
    aurora, // a soft ring whose colour drifts through the spectrum
};
enum class Plates : std::uint8_t
{
    glass,   // the details and hint bars are frosted glass
    outline, // only their rim: no fill
    none,    // neither: the text and glyphs sit straight on the sky
};
struct Chrome
{
    TabStyle tabs = TabStyle::pills;
    FocusRing focus = FocusRing::theme;
    Plates plates = Plates::glass;
    bool badges = true;    // a coloured banner on each cover: GAME, HOMEBREW, MEDIA, PS4
    bool cover_rim = true; // a hairline of light around each cover
};
// ORION's own: plain tabs, the aurora ring, bare bars, banners, no rim.
Chrome orion_chrome();

// One theme. Built-in themes ship as files in the app folder
// (assets/themes/*.json); the player's live in <state_dir>/themes/*.json and
// are read the same way. docs/ORION_THEMES.md describes every key:
//
//   {
//     "format": "orion-theme", "version": 1,
//     "id": "nebula-noir", "name": "Nebula Noir", "author": "you",
//     "extends": "orion",
//     "page":    {"background": "galaxy", "colors": ["#000000", "#02040b", "#3a1f6a", "#0a4a66"]},
//     "palette": {"accent": "#ffc94a", "primary": "#ffc94a"},
//     "orion":   {"clouds": 80, "cover_corners": 28, "tabs": "plain"}
//   }
//
// Every key but id and name is optional: what a file leaves out comes from
// the theme it extends (ORION when it names none).
struct ThemeFile
{
    std::string id;          // lowercase letters, digits, '-' and '_'; unique
    std::string name;        // shown in Settings
    std::string author;      // optional
    std::string description; // optional, one line
    std::string family;      // the design language, as the kit names it
    std::string path;        // the file it came from; empty for one made in code
    bool user = false;       // from the player's folder, not the app's
    // The theme itself. Its id, name, family and summary point into the
    // strings above: call bind() after the struct has moved.
    ui::Theme theme{};
    // ORION's own knobs, which the kit's Theme does not have.
    float clouds = 1.0f;         // 0..2: drifting nebula clouds behind glass
    bool motion = true;          // the backdrop drifts
    float cover_corners = 22.0f; // radius of the covers
    Chrome chrome;

    void bind();
};

// The theme a file starts from: an already loaded file, else a kit theme
// with that id, else ORION. Supplied by the ThemeLibrary while it loads.
using ThemeResolver = std::function<const ThemeFile *(std::string_view id)>;

// Reads one theme document over `base` (what "extends" names, resolved by
// the caller). False, with *error saying why, for anything that is not a
// usable theme; *out is then untouched.
bool parse_theme(std::string_view text, const ThemeResolver &resolve, ThemeFile *out,
                 std::string *error);
// The complete document for a theme: every key written, nothing extended,
// so the file stands on its own.
std::string write_theme(const ThemeFile &file);
// A theme file for one of the kit's built-in themes.
ThemeFile from_kit(const ui::Theme &theme);
// "Nebula Noir!" -> "nebula-noir"; empty when nothing usable is left.
std::string theme_id_from_name(std::string_view name);

// Every theme ORION can wear, in the order Settings lists them: the app's
// own files first (ORION leading), then the player's.
class ThemeLibrary
{
  public:
    // Reads builtin_dir/*.json, then user_dir/*.json. When the app's folder
    // holds no theme files (a PC test run without them) the kit's themes are
    // used instead, so there is always something to wear.
    void load(const std::string &builtin_dir, const std::string &user_dir);

    std::size_t size() const
    {
        return themes_.size();
    }
    const ThemeFile &at(std::size_t index) const
    {
        return *themes_[index];
    }
    const ThemeFile *find(std::string_view id) const;
    // ORION's own theme: what an unknown id falls back to.
    const ThemeFile &fallback() const;
    // Files that were skipped, one line each: "<path>: <why>".
    const std::vector<std::string> &problems() const
    {
        return problems_;
    }

    // Writes the theme to user_dir/<id>.json and adds it (or replaces the
    // player's theme of that id). False, with *error, when it cannot.
    bool save_user_theme(ThemeFile file, std::string *error);

  private:
    void add(std::unique_ptr<ThemeFile> file);
    void read_dir(const std::string &dir, bool user);

    std::vector<std::unique_ptr<ThemeFile>> themes_;
    std::vector<std::string> problems_;
    std::string user_dir_;
};

} // namespace hui::orion
