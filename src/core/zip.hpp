// ORION Media Center - Reading a ZIP archive held in memory (stored and deflated entries).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace hui
{

// Calls `each` for every file in the archive, in the central directory's
// order, with its name ("PPSA99061/eboot.bin") and contents; folders are
// skipped. Stored and deflated entries are read; anything else (encrypted,
// another method, ZIP64) is an error. Stops at the first false from `each`.
// The archive's own integrity is the caller's to check (a published hash).
//
//   unzip(bytes, [&](const std::string &name, std::string_view data) {
//       return save(name, data);
//   }, &error);
using ZipVisitor = std::function<bool(const std::string &name, std::string_view data)>;
bool unzip(std::string_view archive, const ZipVisitor &each, std::string *error);

} // namespace hui
