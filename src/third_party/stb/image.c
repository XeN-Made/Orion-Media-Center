// ps5-homebrew-ui - The single compilation unit for the vendored stb_image.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The failure reason is a plain global rather than thread-local storage: only
// the start-up code decodes, and the console's TLS support is not needed.

#define STB_IMAGE_IMPLEMENTATION 1
#define STBI_NO_THREAD_LOCALS 1
#include "image.h"
