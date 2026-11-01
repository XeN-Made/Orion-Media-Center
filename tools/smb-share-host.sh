#!/usr/bin/env bash
# ORION Media Center - Builds and runs the SMB share server on this PC, to test it from Windows.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tools/smb-share-host.sh <port> <user> <password> <name>=<folder>...
#
# Windows 11 24H2 and later can reach it on another port (its own SMB server
# has 445): net use \\<WSL address>\<name> /user:<user> <password> /TCPPORT:<port>
# `hostname -I` gives the WSL address.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/smb-share-host"
mkdir -p "$out"
"${CC:-clang}" -std=c11 -D_DEFAULT_SOURCE -D_GNU_SOURCE -Wall -Wextra -Werror -O2 -g -pthread \
    -o "$out/smb-share-host" "$root/payload/smbshare/host_main.c" \
    "$root/payload/smbshare/smb_server.c" "$root/payload/smbshare/smb_crypto.c"
exec "$out/smb-share-host" "$@"
