// ORION Media Center - The single compilation unit for the vendored stb_image.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The failure reason is a plain global rather than thread-local storage: the
// console's TLS support is not needed, and a reason read on another thread is
// at worst another picture's.
//
// Large buffers come straight from the system. Decoding a 3840 x 2160
// home-screen picture holds about 70 MB at its peak (the file's data joined
// up, the inflated rows, the image), and the app's own heap is a fixed
// 128 MiB (src/runtime/app_heap.c) that also holds the fonts, the icons and
// the library: a second picture could fail there. Small allocations (icons)
// still use the heap.

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define HUI_STBI_BIG ((size_t)1 << 20)

// In front of every block: its size, and how much was mapped (0: from malloc).
// 16 bytes, so what follows keeps malloc's and mmap's alignment.
typedef struct hui_stbi_block
{
    size_t size;
    size_t mapped;
} hui_stbi_block;

static void *hui_stbi_malloc(size_t size)
{
    const size_t total = size + sizeof(hui_stbi_block);
    hui_stbi_block *block = NULL;
    if (total >= HUI_STBI_BIG)
    {
        void *pages = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (pages == MAP_FAILED)
            return NULL;
        block = (hui_stbi_block *)pages;
        block->mapped = total;
    }
    else
    {
        block = (hui_stbi_block *)malloc(total);
        if (block == NULL)
            return NULL;
        block->mapped = 0;
    }
    block->size = size;
    return block + 1;
}

static void hui_stbi_free(void *memory)
{
    if (memory == NULL)
        return;
    hui_stbi_block *block = (hui_stbi_block *)memory - 1;
    if (block->mapped != 0)
        munmap(block, block->mapped);
    else
        free(block);
}

static void *hui_stbi_realloc(void *memory, size_t size)
{
    if (memory == NULL)
        return hui_stbi_malloc(size);
    const hui_stbi_block *old = (const hui_stbi_block *)memory - 1;
    void *grown = hui_stbi_malloc(size);
    if (grown == NULL)
        return NULL;
    memcpy(grown, memory, old->size < size ? old->size : size);
    hui_stbi_free(memory);
    return grown;
}

#define STBI_MALLOC(size) hui_stbi_malloc(size)
#define STBI_REALLOC(memory, size) hui_stbi_realloc(memory, size)
#define STBI_FREE(memory) hui_stbi_free(memory)

#define STB_IMAGE_IMPLEMENTATION 1
#define STBI_NO_THREAD_LOCALS 1
#include "image.h"
