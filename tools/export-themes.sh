#!/usr/bin/env bash
# ps5-homebrew-ui - Regenerates assets/themes from the kit's built-in themes.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# ORION wears theme files (src/orion/theme_file.hpp, docs/ORION_THEMES.md).
# The built-in ones are generated from src/ui/theme.cpp: run this after
# changing a theme there, and commit what it writes.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cxx=$(command -v "${HOST_CXX:-clang++}")
out="$root/build/host"
mkdir -p "$out"
"$cxx" -std=c++20 -O2 -Wall -Wextra -I"$root/src" -o "$out/export-themes" \
    "$root/tools/export-themes.cpp" "$root/src/ui/theme.cpp" "$root/src/orion/theme_file.cpp" \
    "$root/src/orion/look.cpp" "$root/src/library/json.cpp" "$root/src/core/save_file.cpp"
rm -f "$root"/assets/themes/*.json
"$out/export-themes" "$root/assets/themes"
