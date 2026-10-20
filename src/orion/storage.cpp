// ps5-homebrew-ui - ORION's folders: counting and copying what they hold.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orion/storage.hpp"

#include "core/save_file.hpp"
#include "orion/art_fetcher.hpp"

#include <cstdio>
#include <sys/stat.h>

namespace hui::orion
{

namespace
{

constexpr int kDepth = 4;                   // folders below the one given
constexpr std::size_t kMaxFile = 32u << 20; // bytes of one file
constexpr int kMaxFiles = 20000;

bool ends_with(std::string_view text, std::string_view end)
{
    return text.size() >= end.size() &&
           text.compare(text.size() - end.size(), end.size(), end) == 0;
}

int copy_level(const std::string &from, const std::string &to, int depth, int *budget)
{
    int copied = 0;
    for (const save::DirectoryEntry &entry : save::list_directory(from))
    {
        if (entry.name.empty() || entry.name.front() == '.' || *budget <= 0)
            continue;
        const std::string source = from + "/" + entry.name;
        const std::string target = to + "/" + entry.name;
        if (entry.directory)
        {
            if (depth > 0)
                copied += copy_level(source, target, depth - 1, budget);
            continue;
        }
        struct stat info = {};
        if (::stat(target.c_str(), &info) == 0)
            continue; // already there
        std::string bytes;
        if (!make_directories(to) || !save::read_file(source, &bytes, kMaxFile) ||
            !save::write_atomic(target, bytes).empty())
            continue;
        ++copied;
        --*budget;
    }
    return copied;
}

int count_level(const std::string &folder, std::string_view suffix, int depth)
{
    int count = 0;
    for (const save::DirectoryEntry &entry : save::list_directory(folder))
    {
        if (entry.name.empty() || entry.name.front() == '.')
            continue;
        if (entry.directory)
            count += depth > 0 ? count_level(folder + "/" + entry.name, suffix, depth - 1) : 0;
        else if (suffix.empty() || ends_with(entry.name, suffix))
            ++count;
    }
    return count;
}

} // namespace

int copy_tree(const std::string &from, const std::string &to)
{
    if (from.empty() || to.empty() || from == to)
        return 0;
    int budget = kMaxFiles;
    return copy_level(from, to, kDepth, &budget);
}

int count_files(const std::string &folder, std::string_view suffix)
{
    return count_level(folder, suffix, kDepth);
}

int remove_files(const std::string &folder, std::string_view suffix)
{
    int removed = 0;
    for (const save::DirectoryEntry &entry : save::list_directory(folder))
    {
        if (!entry.directory && ends_with(entry.name, suffix) &&
            std::remove((folder + "/" + entry.name).c_str()) == 0)
            ++removed;
    }
    return removed;
}

} // namespace hui::orion
