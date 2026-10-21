// ORION Media Center - Declarations for the vendored stb_image (see image.c).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// PNG and JPEG, decoded from memory: icons are read with save::read_file
// first. JPEG for the covers PS5SX2 downloads (covers/<serial>.jpg).
#define STBI_ONLY_PNG 1
#define STBI_ONLY_JPEG 1
#define STBI_NO_STDIO 1
#define STBI_NO_LINEAR 1
#define STBI_NO_HDR 1
#include "stb_image.inc"
