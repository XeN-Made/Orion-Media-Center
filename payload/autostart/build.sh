#!/usr/bin/env bash
# ps5-homebrew-ui - Builds ORION's autostart payload with the PS5 payload SDK.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: payload/autostart/build.sh <payload sdk> <output.elf>
#
# Like the launch helper, an ELF for the console's ELF loader, shipped in the
# app folder (assets/payloads): a jailbreak's autoloader sends it after the
# exploit, and it opens ORION (see main.c).

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
    -lSceSystemService -lSceUserService -lkernel_sys
