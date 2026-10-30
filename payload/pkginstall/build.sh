#!/usr/bin/env bash
# ORION Media Center - Builds ORION's package install payload with the PS5 payload SDK.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: payload/pkginstall/build.sh <payload sdk> <output.elf>

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
    -lkernel_sys
