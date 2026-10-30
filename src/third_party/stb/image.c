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

// An animated GIF, one frame at a time: stbi_load_gif_from_memory keeps
// every frame at full size in one block, which a long GIF turns into
// hundreds of megabytes. This keeps the decoder's canvas and the two frames
// before it (a frame may be undone to the one before last), whatever the
// frame count.
struct hui_gif
{
    stbi__context context;
    stbi__gif gif;
    stbi_uc *last;     // the frame given out last ...
    stbi_uc *previous; // ... and the one before it
    int frames;
};

hui_gif *hui_gif_open(const unsigned char *data, int length)
{
    hui_gif *gif = (hui_gif *)STBI_MALLOC(sizeof(hui_gif));
    if (gif == NULL)
        return NULL;
    memset(gif, 0, sizeof(*gif));
    stbi__start_mem(&gif->context, data, length);
    if (!stbi__gif_test(&gif->context))
    {
        STBI_FREE(gif);
        return NULL;
    }
    return gif;
}

const unsigned char *hui_gif_next(hui_gif *gif, int *width, int *height, int *delay_ms)
{
    int channels = 0;
    stbi_uc *two_back = gif->frames >= 2 ? gif->previous : NULL;
    stbi_uc *frame = stbi__gif_load_next(&gif->context, &gif->gif, &channels, 4, two_back);
    if (frame == NULL || frame == (stbi_uc *)&gif->context)
        return NULL; // the end (the context's address), or a broken frame
    const size_t size = (size_t)gif->gif.w * (size_t)gif->gif.h * 4;
    if (gif->last == NULL)
    {
        gif->last = (stbi_uc *)STBI_MALLOC(size);
        gif->previous = (stbi_uc *)STBI_MALLOC(size);
        if (gif->last == NULL || gif->previous == NULL)
            return NULL;
    }
    stbi_uc *older = gif->previous;
    gif->previous = gif->last;
    gif->last = older;
    memcpy(gif->last, frame, size);
    ++gif->frames;
    *width = gif->gif.w;
    *height = gif->gif.h;
    *delay_ms = gif->gif.delay;
    return gif->last;
}

void hui_gif_close(hui_gif *gif)
{
    if (gif == NULL)
        return;
    STBI_FREE(gif->gif.out);
    STBI_FREE(gif->gif.history);
    STBI_FREE(gif->gif.background);
    STBI_FREE(gif->last);
    STBI_FREE(gif->previous);
    STBI_FREE(gif);
}
