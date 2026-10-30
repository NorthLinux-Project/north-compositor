#include "util/arena.h"

#include <stdlib.h>
#include <string.h>

#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))
#include <sanitizer/asan_interface.h>
#else
#define ASAN_POISON_MEMORY_REGION(addr, size) ((void)(addr), (void)(size))
#define ASAN_UNPOISON_MEMORY_REGION(addr, size) ((void)(addr), (void)(size))
#endif

Arena arena_create(size_t capacity)
{
    Arena arena = {0};
    arena.buffer = (unsigned char *)malloc(capacity);
    if (arena.buffer) {
        arena.capacity = capacity;
        arena.offset = 0;
        ASAN_POISON_MEMORY_REGION(arena.buffer, capacity);
    }
    return arena;
}

void arena_destroy(Arena *arena)
{
    if (arena && arena->buffer) {
        ASAN_UNPOISON_MEMORY_REGION(arena->buffer, arena->capacity);
        free(arena->buffer);
        arena->buffer = NULL;
        arena->capacity = 0;
        arena->offset = 0;
    }
}

void arena_reset(Arena *arena)
{
    if (arena && arena->buffer) {
        ASAN_POISON_MEMORY_REGION(arena->buffer, arena->capacity);
        arena->offset = 0;
    }
}

void *arena_alloc(Arena *arena, size_t size, size_t alignment)
{
    if (!arena || !arena->buffer || size == 0) {
        return NULL;
    }

    if (alignment == 0) {
        alignment = ARENA_DEFAULT_ALIGNMENT;
    }

    uintptr_t current_addr = (uintptr_t)(arena->buffer + arena->offset);
    uintptr_t aligned_addr = (current_addr + (alignment - 1)) & ~(alignment - 1);
    size_t padding = (size_t)(aligned_addr - current_addr);

    if (arena->offset + padding + size > arena->capacity) {
        return NULL;
    }

    arena->offset += padding + size;
    void *ptr = (void *)aligned_addr;
    ASAN_UNPOISON_MEMORY_REGION(ptr, size);
    return ptr;
}

void *arena_alloc_zero(Arena *arena, size_t size, size_t alignment)
{
    void *ptr = arena_alloc(arena, size, alignment);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

ArenaTemp arena_temp_begin(Arena *arena)
{
    ArenaTemp temp;
    temp.arena = arena;
    temp.offset = arena ? arena->offset : 0;
    return temp;
}

void arena_temp_end(ArenaTemp temp)
{
    if (temp.arena && temp.offset <= temp.arena->offset) {
        size_t rolled_back_bytes = temp.arena->offset - temp.offset;
        unsigned char *rollback_start = temp.arena->buffer + temp.offset;
        ASAN_POISON_MEMORY_REGION(rollback_start, rolled_back_bytes);
        temp.arena->offset = temp.offset;
    }
}

#define SCRATCH_ARENA_CAPACITY (1024 * 1024 * 8)
static _Thread_local Arena tls_scratch_pool[2] = {0};
static _Thread_local bool tls_scratch_initialized = false;

static void init_scratch_pool(void)
{
    if (!tls_scratch_initialized) {
        tls_scratch_pool[0] = arena_create(SCRATCH_ARENA_CAPACITY);
        tls_scratch_pool[1] = arena_create(SCRATCH_ARENA_CAPACITY);
        tls_scratch_initialized = true;
    }
}

ArenaTemp get_scratch(Arena **conflicts, size_t conflict_count)
{
    init_scratch_pool();

    Arena *selected = &tls_scratch_pool[0];
    for (size_t i = 0; i < conflict_count; ++i) {
        if (conflicts[i] == selected) {
            selected = &tls_scratch_pool[1];
            break;
        }
    }

    return arena_temp_begin(selected);
}
