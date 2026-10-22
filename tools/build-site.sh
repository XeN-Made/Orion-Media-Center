#!/bin/sh
# ORION Media Center - Assemble the project site (GitHub Pages) from site/ and the docs.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: tools/build-site.sh [output dir]   (default: build/site)
#
# The pages live in site/; the pictures, the theme template, the fonts and
# the icon are copied in from where the repository keeps them, so there is one
# copy of each. Open <output dir>/index.html, or serve the folder:
#   python3 -m http.server -d build/site 8000
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-"$root/build/site"}

rm -rf "$out"
mkdir -p "$out/screenshots" "$out/fonts"
cp "$root"/site/*.html "$root"/site/*.css "$out/"
cp "$root"/docs/screenshots/*.jpg "$out/screenshots/"
cp "$root/docs/themes/template.theme" "$out/template.theme"
cp "$root/sce_sys/icon0.png" "$out/icon.png"
for font in Inter-Regular Inter-SemiBold Montserrat-Medium; do
    cp "$root/third_party/fonts/$font.ttf" "$out/fonts/"
done
cp "$root/third_party/fonts/Inter-LICENSE.txt" "$root/third_party/fonts/Montserrat-LICENSE.txt" "$out/fonts/"
# GitHub Pages would otherwise run the folder through Jekyll.
touch "$out/.nojekyll"
echo "site: $out"
