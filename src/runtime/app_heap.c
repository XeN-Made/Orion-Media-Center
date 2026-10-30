// ORION Media Center - Process-lifetime heap for the OpenGL runtime.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Adapted from ps5-opengl native-app/app_heap.c. tools/build.sh links with
// --wrap for the malloc family whenever this file exists, routing allocations
// into a fixed 128 MiB sceLibcMspace that is never unmapped. When it is full
// an allocation goes to the system's heap instead (free tells them apart by
// address): a failed allocation under C++ without exceptions ends the app.
//
// A block of 16 MiB or more gets a mapping of its own, unmapped when freed.
// An archive's decoder wants two such at once (a RAR's 32 MiB window and its
// copy, made as its first file starts); with the app's own 60-odd MiB in the
// mspace the second one did not fit, the system's heap is small, and the app
// ended there, twice, at the same place.

#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *address, size_t size);
void __real_free(void *address);
int __real_posix_memalign(void **address, size_t alignment, size_t size);
size_t __real_malloc_usable_size(const void *address);

void *sceLibcMspaceCreate(const char *name, void *base, size_t size, unsigned flags);
void *sceLibcMspaceMalloc(void *mspace, size_t size);
void *sceLibcMspaceCalloc(void *mspace, size_t count, size_t size);
void *sceLibcMspaceRealloc(void *mspace, void *address, size_t size);
void sceLibcMspaceFree(void *mspace, void *address);
int sceLibcMspacePosixMemalign(void *mspace, void **address, size_t alignment, size_t size);
size_t sceLibcMspaceMallocUsableSize(const void *address);

/* Fixed, process-lifetime heap: do not unmap underneath late C++ destructors. */
#define HUI_HEAP_SIZE (128u * 1024u * 1024u)

static atomic_int hui_heap_state;
static void *hui_heap_base;
static void *hui_heap_mspace;
/* Owned-heap usable bytes only; relaxed counters are observations, not fences. */
static atomic_size_t hui_heap_live_bytes, hui_heap_peak_bytes, hui_heap_blocks;
static atomic_size_t hui_heap_failures;

static void hui_heap_resize_stats(size_t before, size_t after)
{
    size_t live = after >= before ? atomic_fetch_add_explicit(&hui_heap_live_bytes, after - before,
                                                              memory_order_relaxed) +
                                        after - before
                                  : atomic_fetch_sub_explicit(&hui_heap_live_bytes, before - after,
                                                              memory_order_relaxed) -
                                        (before - after);
    size_t peak = atomic_load_explicit(&hui_heap_peak_bytes, memory_order_relaxed);
    while (live > peak &&
           !atomic_compare_exchange_weak_explicit(&hui_heap_peak_bytes, &peak, live,
                                                  memory_order_relaxed, memory_order_relaxed))
    {
    }
}

static void *hui_heap_record_allocation(void *address, int nonzero)
{
    if (address)
    {
        hui_heap_resize_stats(0, sceLibcMspaceMallocUsableSize(address));
        atomic_fetch_add_explicit(&hui_heap_blocks, 1, memory_order_relaxed);
    }
    else if (nonzero)
    {
        atomic_fetch_add_explicit(&hui_heap_failures, 1, memory_order_relaxed);
    }
    return address;
}

static int hui_heap_ready(void)
{
    int state = atomic_load_explicit(&hui_heap_state, memory_order_acquire);
    if (state == 2)
        return 1;
    if (state != 0)
        return 0;

    int expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&hui_heap_state, &expected, 1,
                                                 memory_order_acq_rel, memory_order_acquire))
        return expected == 2;

    void *base = mmap(NULL, HUI_HEAP_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED)
    {
        atomic_store_explicit(&hui_heap_state, -1, memory_order_release);
        return 0;
    }

    hui_heap_base = base;
    hui_heap_mspace = sceLibcMspaceCreate("orion-media-center", base, HUI_HEAP_SIZE, 0);
    if (hui_heap_mspace == NULL)
    {
        hui_heap_base = NULL;
        munmap(base, HUI_HEAP_SIZE);
        atomic_store_explicit(&hui_heap_state, -1, memory_order_release);
        return 0;
    }

    atomic_store_explicit(&hui_heap_state, 2, memory_order_release);
    return 1;
}

static int hui_heap_owns(const void *address)
{
    /* Acquire publication before reading non-atomic heap metadata. */
    if (atomic_load_explicit(&hui_heap_state, memory_order_acquire) != 2)
        return 0;
    uintptr_t value = (uintptr_t)address;
    uintptr_t base = (uintptr_t)hui_heap_base;
    return value >= base && value - base < HUI_HEAP_SIZE;
}

/* Blocks with a mapping of their own: few, large, and short-lived. */
#define HUI_BIG_BLOCK (16u * 1024u * 1024u)
#define HUI_BIG_SLOTS 32

static struct
{
    void *address;
    size_t size;
} hui_big[HUI_BIG_SLOTS];
static atomic_flag hui_big_lock = ATOMIC_FLAG_INIT;

static void hui_big_take(void)
{
    while (atomic_flag_test_and_set_explicit(&hui_big_lock, memory_order_acquire))
    {
    }
}

static void hui_big_give(void)
{
    atomic_flag_clear_explicit(&hui_big_lock, memory_order_release);
}

/* A zeroed mapping of its own, or NULL (none left, or no slot). */
static void *hui_big_alloc(size_t size)
{
    void *address = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (address == MAP_FAILED)
        return NULL;
    hui_big_take();
    for (int slot = 0; slot < HUI_BIG_SLOTS; ++slot)
    {
        if (hui_big[slot].address == NULL)
        {
            hui_big[slot].address = address;
            hui_big[slot].size = size;
            hui_big_give();
            hui_heap_resize_stats(0, size);
            atomic_fetch_add_explicit(&hui_heap_blocks, 1, memory_order_relaxed);
            return address;
        }
    }
    hui_big_give();
    munmap(address, size);
    return NULL;
}

/* The size of a block mapped by hui_big_alloc, or 0 when it is not one. */
static size_t hui_big_size(const void *address)
{
    size_t size = 0;
    hui_big_take();
    for (int slot = 0; slot < HUI_BIG_SLOTS; ++slot)
    {
        if (hui_big[slot].address == address)
        {
            size = hui_big[slot].size;
            break;
        }
    }
    hui_big_give();
    return size;
}

/* Unmaps a block of hui_big_alloc's; false when it is not one. */
static int hui_big_free(void *address)
{
    size_t size = 0;
    hui_big_take();
    for (int slot = 0; slot < HUI_BIG_SLOTS; ++slot)
    {
        if (hui_big[slot].address == address)
        {
            size = hui_big[slot].size;
            hui_big[slot].address = NULL;
            hui_big[slot].size = 0;
            break;
        }
    }
    hui_big_give();
    if (size == 0)
        return 0;
    munmap(address, size);
    hui_heap_resize_stats(size, 0);
    atomic_fetch_sub_explicit(&hui_heap_blocks, 1, memory_order_relaxed);
    return 1;
}

void __wrap_free(void *address);

void *__wrap_malloc(size_t size)
{
    if (!hui_heap_ready())
        return __real_malloc(size);
    if (size >= HUI_BIG_BLOCK)
    {
        void *big = hui_big_alloc(size);
        if (big != NULL)
            return big;
    }
    void *address =
        hui_heap_record_allocation(sceLibcMspaceMalloc(hui_heap_mspace, size), size != 0);
    return address != NULL || size == 0 ? address : __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size)
{
    if (!hui_heap_ready())
        return __real_calloc(count, size);
    if (size != 0 && count >= HUI_BIG_BLOCK / size && count <= SIZE_MAX / size)
    {
        void *big = hui_big_alloc(count * size); // a fresh mapping is zeroed
        if (big != NULL)
            return big;
    }
    void *address = hui_heap_record_allocation(sceLibcMspaceCalloc(hui_heap_mspace, count, size),
                                               count != 0 && size != 0);
    return address != NULL || count == 0 || size == 0 ? address : __real_calloc(count, size);
}

void *__wrap_realloc(void *address, size_t size)
{
    if (address == NULL)
        return __wrap_malloc(size);
    const size_t big = hui_big_size(address);
    if (big != 0)
    {
        if (size == 0)
        {
            hui_big_free(address);
            return NULL;
        }
        void *result = __wrap_malloc(size);
        if (result != NULL)
        {
            memcpy(result, address, big < size ? big : size);
            hui_big_free(address);
        }
        return result;
    }
    if (!hui_heap_owns(address))
        return __real_realloc(address, size);
    size_t before = sceLibcMspaceMallocUsableSize(address);
    void *result = sceLibcMspaceRealloc(hui_heap_mspace, address, size);
    if (result)
        hui_heap_resize_stats(before, sceLibcMspaceMallocUsableSize(result));
    else if (size)
    {
        atomic_fetch_add_explicit(&hui_heap_failures, 1, memory_order_relaxed);
        // Moved out (a mapping of its own, or the system's heap); the old
        // block stays valid on failure.
        result = size >= HUI_BIG_BLOCK ? hui_big_alloc(size) : NULL;
        if (result == NULL)
            result = __real_malloc(size);
        if (result)
        {
            memcpy(result, address, before < size ? before : size);
            __wrap_free(address);
        }
    }
    return result;
}

void __wrap_free(void *address)
{
    if (hui_heap_owns(address))
    {
        hui_heap_resize_stats(sceLibcMspaceMallocUsableSize(address), 0);
        atomic_fetch_sub_explicit(&hui_heap_blocks, 1, memory_order_relaxed);
        sceLibcMspaceFree(hui_heap_mspace, address);
    }
    else if (address != NULL && !hui_big_free(address))
    {
        __real_free(address);
    }
}

int __wrap_posix_memalign(void **address, size_t alignment, size_t size)
{
    if (!hui_heap_ready())
        return __real_posix_memalign(address, alignment, size);
    if (size >= HUI_BIG_BLOCK && alignment <= 4096)
    {
        void *big = hui_big_alloc(size); // mappings start on a page
        if (big != NULL)
        {
            *address = big;
            return 0;
        }
    }
    int result = sceLibcMspacePosixMemalign(hui_heap_mspace, address, alignment, size);
    if (result == 0)
        hui_heap_record_allocation(*address, size != 0);
    else if (result == ENOMEM)
    {
        atomic_fetch_add_explicit(&hui_heap_failures, 1, memory_order_relaxed);
        result = __real_posix_memalign(address, alignment, size);
    }
    return result;
}

size_t __wrap_malloc_usable_size(const void *address)
{
    if (hui_heap_owns(address))
        return sceLibcMspaceMallocUsableSize(address);
    const size_t big = address != NULL ? hui_big_size(address) : 0;
    return big != 0 ? big : __real_malloc_usable_size(address);
}

/* Diagnostics: current heap counters for logs and the diagnostics screen. */
void hui_heap_stats(size_t *live_bytes, size_t *peak_bytes, size_t *blocks, size_t *failures)
{
    if (live_bytes)
        *live_bytes = atomic_load_explicit(&hui_heap_live_bytes, memory_order_relaxed);
    if (peak_bytes)
        *peak_bytes = atomic_load_explicit(&hui_heap_peak_bytes, memory_order_relaxed);
    if (blocks)
        *blocks = atomic_load_explicit(&hui_heap_blocks, memory_order_relaxed);
    if (failures)
        *failures = atomic_load_explicit(&hui_heap_failures, memory_order_relaxed);
}
