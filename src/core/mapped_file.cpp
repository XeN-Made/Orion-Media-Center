// ORION Media Center - Large files and buffers mapped outside the heap.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/mapped_file.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace hui
{

MappedFile::~MappedFile()
{
    if (data_ != nullptr)
        ::munmap(data_, size_);
}

bool MappedFile::open(const std::string &path, std::string *error)
{
    if (data_ != nullptr)
        ::munmap(data_, size_);
    data_ = nullptr;
    size_ = 0;
    const int fd = ::open(path.c_str(), O_RDONLY);
    struct stat info = {};
    if (fd < 0 || ::fstat(fd, &info) != 0 || info.st_size <= 0)
    {
        if (error != nullptr)
            *error = "cannot read " + path + " (errno " + std::to_string(errno) + ")";
        if (fd >= 0)
            ::close(fd);
        return false;
    }
    const auto size = static_cast<std::size_t>(info.st_size);
    void *data = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED)
    {
        // A file system that maps no files: read it into anonymous memory,
        // which every system here maps (the app's heap is such a mapping).
        data = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        std::size_t done = 0;
        while (data != MAP_FAILED && done < size)
        {
            const ssize_t got = ::read(fd, static_cast<char *>(data) + done, size - done);
            if (got < 0 && errno == EINTR)
                continue;
            if (got <= 0)
            {
                ::munmap(data, size);
                data = MAP_FAILED;
                break;
            }
            done += static_cast<std::size_t>(got);
        }
    }
    ::close(fd); // a mapping keeps the file
    if (data == MAP_FAILED)
    {
        if (error != nullptr)
            *error = "cannot read " + path + " into memory (errno " + std::to_string(errno) + ")";
        return false;
    }
    data_ = data;
    size_ = size;
    return true;
}

MappedBuffer::~MappedBuffer()
{
    release();
}

void MappedBuffer::release()
{
    if (data_ != nullptr)
        ::munmap(data_, size_);
    data_ = nullptr;
    size_ = 0;
}

bool MappedBuffer::reserve(std::size_t size)
{
    if (size <= size_ && data_ != nullptr)
        return true;
    release();
    void *data = ::mmap(nullptr, size == 0 ? 1 : size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    if (data == MAP_FAILED)
        return false;
    data_ = data;
    size_ = size == 0 ? 1 : size;
    return true;
}

} // namespace hui
