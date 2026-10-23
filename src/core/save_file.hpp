// ORION Media Center - Checked save container and atomic file writes.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hui::save
{

// Kinds of payload stored in the container.
enum class Kind : std::uint16_t
{
    settings = 1,
    library = 2,
    stats = 3,
    game = 4,
    prefs = 5,
};

// Layout (little-endian): "HUIL" | u16 version | u16 kind | u32 size |
// payload | u32 CRC-32 of everything before it.
std::string encode(Kind kind, std::uint16_t version, std::string_view payload);

struct Decoded
{
    bool ok = false;
    std::uint16_t version = 0;
    std::string payload;
    std::string error;
};

// Rejects a wrong magic, kind, size, checksum or trailing bytes.
Decoded decode(Kind kind, std::string_view data);

std::uint32_t crc32(std::string_view data);

// Writes path via "path.tmp": write, fsync, close, rename (unlinking and
// retrying if the rename fails). Returns an error or "".
std::string write_atomic(const std::string &path, std::string_view data);

// Reads a whole file of at most max_bytes. Returns false if it is missing or
// unreadable.
bool read_file(const std::string &path, std::string *data, std::size_t max_bytes = 4u << 20);

struct DirectoryEntry
{
    std::string name;
    bool directory = false;
};

// What happened while listing, for diagnosing a folder that comes back empty.
struct ListingStats
{
    int open_error = 0;    // errno of a failed open, else 0
    int read_error = 0;    // errno of a failed getdents, else 0
    int reads = 0;         // getdents calls (console only)
    int records = 0;       // named records seen, "." and ".." included
    int empty_records = 0; // records with no file number (skipped)
    int stat_failures = 0; // records whose type had to be looked up and could not be
    int stat_error = 0;    // errno of the first of those
};

// Every entry of a directory except "." and "..", sorted by name; empty when
// it cannot be opened. On the console the C library's readdir returns
// nothing, so the kernel's records are read directly (getdents).
std::vector<DirectoryEntry> list_directory(const std::string &directory,
                                           ListingStats *stats = nullptr);

// Names of the files in a directory. Reads the build-generated index.txt
// (one name per line) when present; otherwise lists the directory.
std::vector<std::string> list_files(const std::string &directory);

// Creates a directory if it does not exist (single level).
bool ensure_directory(const std::string &path);
// Folders made, and files written (write_atomic), are open to every user:
// on a console an FTP server runs as another user than the app and must be
// able to add, replace and delete them. This opens up a tree made before
// that (folders 0777, files 0666; links are not followed) and returns how
// many entries changed. Refuses a relative path, "/" and one with "..".
int open_up_tree(const std::string &root);

// Removes a directory and everything in it. Refuses a relative path, "/"
// and one with ".."; true when nothing of it is left.
bool remove_tree(const std::string &path);

} // namespace hui::save
