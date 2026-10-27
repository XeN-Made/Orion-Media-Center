// ORION Media Center - ZIP archives in memory: reading them, and storing files in one.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hui
{

// Calls `each` for every file in the archive, in the central directory's
// order, with its name ("PPSA99062/eboot.bin") and contents; folders are
// skipped. Stored and deflated entries are read; anything else (encrypted,
// another method, ZIP64) is an error. Stops at the first false from `each`.
// The archive's own integrity is the caller's to check (a published hash).
//
//   unzip(bytes, [&](const std::string &name, std::string_view data) {
//       return save(name, data);
//   }, &error);
using ZipVisitor = std::function<bool(const std::string &name, std::string_view data)>;
bool unzip(std::string_view archive, const ZipVisitor &each, std::string *error);

// A ZIP holding these files (name, contents), stored as they are: what a
// browser saves and any unzip opens. Meant for a few megabytes of text.
//
//   const std::string archive = zip_files({{"boot.txt", boot}, {"art.txt", art}});
std::string zip_files(const std::vector<std::pair<std::string, std::string>> &files);

} // namespace hui
