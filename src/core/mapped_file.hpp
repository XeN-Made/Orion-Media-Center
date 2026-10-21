// ORION Media Center - Large files and buffers mapped outside the heap.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace hui
{

// A file mapped read-only. The app's heap is a fixed 128 MB (src/runtime):
// a 40 MB download read into a string, beside everything else, does not fit;
// a mapping costs address space, not heap.
//
//   MappedFile zip;
//   if (zip.open(path, &error)) check(zip.view());
class MappedFile
{
  public:
    MappedFile() = default;
    ~MappedFile();
    MappedFile(const MappedFile &) = delete;
    MappedFile &operator=(const MappedFile &) = delete;

    bool open(const std::string &path, std::string *error);
    std::string_view view() const
    {
        return {static_cast<const char *>(data_), size_};
    }

  private:
    void *data_ = nullptr;
    std::size_t size_ = 0;
};

// Writable memory of its own mapping, for one large buffer, given back when
// it goes.
class MappedBuffer
{
  public:
    MappedBuffer() = default;
    ~MappedBuffer();
    MappedBuffer(const MappedBuffer &) = delete;
    MappedBuffer &operator=(const MappedBuffer &) = delete;

    // At least `size` bytes; the old contents go. False when there is no room.
    bool reserve(std::size_t size);
    char *data()
    {
        return static_cast<char *>(data_);
    }

  private:
    void release();
    void *data_ = nullptr;
    std::size_t size_ = 0;
};

} // namespace hui
