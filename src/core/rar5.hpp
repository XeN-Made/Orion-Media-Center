// ORION Media Center - A RAR5 archive whose files are stored as they are: where each one's bytes
// lie. Copyright (C) 2026 BlackBearReloaded SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hui
{

// Most PS5 games come as RARs made with "store" (their files are already
// compressed): every file's bytes lie in the archive as they are, between
// headers. Such an archive needs no decoder: the headers say where each file
// is, and it is copied straight off the drive. libarchive 3.7.4 (the console's)
// ends the app partway through one, and its 64 KB pieces are slow besides.
//
// The whole archive is planned before anything is written, so an archive
// that needs unpacking after all (a compressed or encrypted file, headers it
// does not know) goes to libarchive untouched.

// `size` bytes from `offset` of volume `volume` (fewer at its end, none past
// it) into `out`; false when the drive would not give them.
using Rar5ReadAt = std::function<bool(std::size_t volume, std::uint64_t offset, std::size_t size,
                                      std::string *out)>;

// A run of a file's bytes in one volume.
struct Rar5Piece
{
    std::size_t volume = 0;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

struct Rar5File
{
    std::string name; // as the archive has it ('/' between folders)
    bool directory = false;
    std::uint64_t size = 0;        // its pieces together
    std::vector<Rar5Piece> pieces; // in order: one per volume it spans
};

enum class Rar5Plan : unsigned char
{
    stored,     // every file as it is: copy the pieces
    not_rar5,   // another kind of archive (RAR4, ZIP, a self-extracting one)
    unpack,     // compressed, encrypted or unknown: for libarchive
    unreadable, // the drive would not give a header
    damaged,    // a header fails its checksum or is cut short
};

// Plans an archive of `volumes` volumes in order (part1, part2, ...). On
// stored, `files` holds every file and folder; otherwise `why` says what
// was found.
Rar5Plan plan_rar5(std::size_t volumes, const Rar5ReadAt &read_at, std::vector<Rar5File> *files,
                   std::string *why);

} // namespace hui
