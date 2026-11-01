#!/usr/bin/env bash
# ORION Media Center - Builds the payload that shares the console's drives over SMB.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: payload/smbshare/build.sh <payload sdk> <output.elf>
#
# The server itself (smb_server.c, smb_crypto.c) is portable C; the PC runs
# it too (tools/smb-share-host.sh) and the unit tests check its parts.

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
"$compiler" -Wall -Wextra -Werror -O2 -o "$output" "$here/main.c" "$here/smb_server.c" \
    "$here/smb_crypto.c" -lkernel_sys
