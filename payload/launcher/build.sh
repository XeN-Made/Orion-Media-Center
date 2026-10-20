#!/usr/bin/env bash
# ps5-homebrew-ui - Builds ORION's launch helper payload with the PS5 payload SDK.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: payload/launcher/build.sh <payload sdk> <output.elf>
#
# The helper is an ELF for the console's ELF loader, not part of the app:
# it is compiled with the payload SDK's own compiler and shipped inside the
# app folder (assets/payloads), from where ORION sends it to 127.0.0.1:9021.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=${1:?usage: build.sh <payload sdk> <output.elf>}
output=${2:?usage: build.sh <payload sdk> <output.elf>}

compiler="$sdk/bin/prospero-clang"
export PS5_PAYLOAD_SDK="$sdk"
[[ -x $compiler ]] || {
    echo "payload compiler not found: $compiler" >&2
    exit 2
}
mkdir -p "$(dirname -- "$output")"
"$compiler" -Wall -Wextra -Werror -O2 -o "$output" "$here/main.c" \
    -lSceSystemService -lSceUserService -lScePad -lkernel_sys
