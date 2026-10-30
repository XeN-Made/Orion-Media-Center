// ORION Media Center - Declarations for the vendored stb_image (see image.c).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// PNG and JPEG, decoded from memory: icons are read with save::read_file
// first. JPEG for the covers PS5SX2 downloads (covers/<serial>.jpg), TGA for
// PS5-ECAM's (covers/boxart/<side>/<game ID>.tga). GIF for backgrounds that
// move (library/animation.hpp); a GIF read as a picture is its first frame.
#define STBI_ONLY_PNG 1
#define STBI_ONLY_JPEG 1
#define STBI_ONLY_TGA 1
#define STBI_ONLY_GIF 1
#define STBI_NO_STDIO 1
#define STBI_NO_LINEAR 1
#define STBI_NO_HDR 1
#include "stb_image.inc"

#ifdef __cplusplus
extern "C" {
#endif

// An animated GIF read one frame at a time (image.c). The data must outlive
// it. hui_gif_next gives the next whole frame (RGBA8, width * height * 4,
// valid until the next call) or NULL at the end or on a broken frame.
typedef struct hui_gif hui_gif;
hui_gif *hui_gif_open(const unsigned char *data, int length);
const unsigned char *hui_gif_next(hui_gif *gif, int *width, int *height, int *delay_ms);
void hui_gif_close(hui_gif *gif);

#ifdef __cplusplus
}
#endif
