#!/usr/bin/env bash
# ORION Media Center - Builds ORION's play time reader payload with the PS5 payload SDK.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: payload/playtime/build.sh <payload sdk> <PacBrew sysroot> <output.elf>
#
# It reads the system's database with PacBrew's SQLite, which makes it a few
# megabytes: a payload of its own, so the launch helper sent on every launch
# stays small.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=${1:?usage: build.sh <payload sdk> <PacBrew sysroot> <output.elf>}
sysroot=${2:?usage: build.sh <payload sdk> <PacBrew sysroot> <output.elf>}
output=${3:?usage: build.sh <payload sdk> <PacBrew sysroot> <output.elf>}

compiler="$sdk/bin/prospero-clang"
export PS5_PAYLOAD_SDK="$sdk"
[[ -x $compiler ]] || {
    echo "payload compiler not found: $compiler" >&2
    exit 2
}
homebrew="$sysroot/user/homebrew"
[[ -f $homebrew/lib/libsqlite3.a && -f $homebrew/include/sqlite3.h ]] || {
    echo "PacBrew's SQLite not found under $homebrew" >&2
    exit 2
}
mkdir -p "$(dirname -- "$output")"
"$compiler" -Wall -Wextra -Werror -O2 -I"$homebrew/include" -o "$output" "$here/main.c" \
    "$homebrew/lib/libsqlite3.a" -lkernel_sys
