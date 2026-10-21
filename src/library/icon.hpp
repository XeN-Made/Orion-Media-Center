// ps5-homebrew-ui - Title icons: PNG files turned into textures for the covers.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "library/library.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace hui::gfx
{
class GlBatch;
}

namespace hui::library
{

// Icons are kept at most this many pixels on a side: a cover is about 220
// virtual pixels wide, and a library of a hundred 512 x 512 icons would hold
// a hundred megabytes of textures.
constexpr int kIconSize = 256;

struct Image
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba; // width * height * 4, top row first
};

// Decodes a PNG (any bit depth or colour type) to RGBA8 and box-filters it
// down by a whole factor until neither side exceeds max_size. False, with
// *out emptied, for anything that is not a readable PNG.
bool decode_png(std::string_view bytes, int max_size, Image *out);
// Why the last decode_png failed, as stb_image says it ("outofmem", ...).
const char *png_failure();

// Reads, decodes and uploads every title's icon (needs a GL context); titles
// whose icon is missing or broken keep texture 0. Returns the count uploaded.
// Games for an emulator are left out: there can be thousands, so their box
// art is uploaded only once a design wants it on screen (load_wanted_icons).
int load_icons(Library &library, gfx::GlBatch &batch);
// The icons of these titles (by id), when they have none uploaded yet.
int load_wanted_icons(Library &library, const std::vector<std::string> &ids, gfx::GlBatch &batch);

// Big covers (Cases view) are drawn from a sharp copy of the picture, up to
// this many pixels on a side, kept only for the few titles on screen.
constexpr int kSharpSize = 1024;
// Makes the sharp copies match `ids`: releases those of titles not in it and
// uploads at most `budget` new ones (a decode is a few milliseconds; spread
// over frames, scrolling stays smooth). Returns how many it uploaded.
int load_sharp_icons(Library &library, const std::vector<std::string> &ids, gfx::GlBatch &batch,
                     int budget);

} // namespace hui::library
