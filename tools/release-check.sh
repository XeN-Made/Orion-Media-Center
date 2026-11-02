#!/usr/bin/env bash
# ORION Media Center - Everything a release needs before it is tagged, checked in one go.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tools/release-check.sh
#
# The version in sce_sys/param.json is new (above the last tag, not tagged
# yet) and is the one assets/whats-new.txt describes; the working tree has
# nothing uncommitted; the tests, the PS5 build and lint pass; the music
# meets its loudness rules. It changes nothing, tags nothing and pushes
# nothing. One line a check, then the verdict; exit status 1 if any failed.

set -uo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root" || exit 1
log_dir="$root/build/release-check"
mkdir -p "$log_dir"
failed=0

result() { # result <PASS|FAIL> <name> <detail>
    printf '%s %s: %s\n' "$1" "$2" "$3"
    [[ $1 == PASS ]] || failed=$((failed + 1))
}

# Runs a command quietly; its output goes to build/release-check/<name>.log.
step() { # step <name> <command...>
    local name=$1
    shift
    if "$@" >"$log_dir/$name.log" 2>&1; then
        result PASS "$name" "$(tail -n 1 "$log_dir/$name.log")"
    else
        result FAIL "$name" "see build/release-check/$name.log: $(tail -n 1 "$log_dir/$name.log")"
    fi
}

version=$(python3 -c 'import json; print(json.load(open("sce_sys/param.json"))["contentVersion"])')
described=$(sed -n '1s/^# *//p' assets/whats-new.txt | tr -d '\r')
last_tag=$(git tag --list '[0-9][0-9].[0-9][0-9][0-9].[0-9][0-9][0-9]' --sort=-version:refname |
    head -n 1)

if [[ $version == "$described" ]]; then
    result PASS whats-new "describes $version"
else
    result FAIL whats-new "param.json says $version, whats-new.txt describes $described"
fi
if git rev-parse -q --verify "refs/tags/$version" >/dev/null; then
    result FAIL version "$version is tagged already: raise contentVersion in param.json"
elif [[ -n $last_tag && $(printf '%s\n%s\n' "$last_tag" "$version" | sort -V | tail -n 1) != "$version" ]]; then
    result FAIL version "$version is not above the last release, $last_tag"
else
    result PASS version "$version, after ${last_tag:-no release yet}"
fi
if [[ -z $(git status --porcelain --untracked-files=no) ]]; then
    result PASS tree "nothing uncommitted on $(git branch --show-current)"
else
    result FAIL tree "uncommitted changes: $(git status --porcelain --untracked-files=no | wc -l) files"
fi

step tests make test-unit
step build make
step lint make lint
step music python3 tools/audio-check.py

if ((failed == 0)); then
    echo "Ready to release $version (tag and push only when the owner says so)"
else
    echo "Not ready: $failed check(s) failed"
    exit 1
fi
