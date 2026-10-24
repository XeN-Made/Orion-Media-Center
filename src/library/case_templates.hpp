// ORION Media Center - Case templates: a player's case picture with the cover in its window.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "library/icon.hpp"

#include <string>
#include <string_view>

namespace hui::library
{

// Where in a case picture the cover shows, as fractions of its width and
// height (0..1, from the top left).
struct CaseWindow
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

// The cover window of a case picture: the clear rectangle around its middle
// (alpha below a quarter). Its edges are found along the middle row and
// column, so the window is a rectangle, as a case's sleeve is. False when the
// middle is not clear (a picture with no window) or the window is too small to
// hold a cover (under a third of each side).
bool find_case_window(const Image &picture, CaseWindow *window);

// The name of a title's template in <state_dir>/cases, without ".png": "ps5"
// and "ps4" for console titles, "homebrew" and "media", or for a game its
// system's short name in lower case without spaces ("ps2", "wiiu", "gba").
// alias is a second name to try for a game (ES-DE's: "gc", "n3ds"), or empty.
std::string case_key(const Title &title, std::string *alias = nullptr);

} // namespace hui::library
