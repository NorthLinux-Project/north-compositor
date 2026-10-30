#ifndef ARENA_H
#define ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARENA_DEFAULT_ALIGNMENT sizeof(void *)

typedef struct {
    unsigned char *buffer;
    size_t capacity;
    size_t offset;
} Arena;

typedef struct {
    Arena *arena;
    size_t offset;
} ArenaTemp;

Arena arena_create(size_t capacity);
void arena_destroy(Arena *arena);
void arena_reset(Arena *arena);

void *arena_alloc(Arena *arena, size_t size, size_t alignment);
void *arena_alloc_zero(Arena *arena, size_t size, size_t alignment);

ArenaTemp arena_temp_begin(Arena *arena);
void arena_temp_end(ArenaTemp temp);

ArenaTemp get_scratch(Arena **conflicts, size_t conflict_count);

#define arena_push_type(arena, Type) ((Type *)arena_alloc((arena), sizeof(Type), _Alignof(Type)))

#define arena_push_array(arena, Type, count)                                                       \
    ((Type *)arena_alloc((arena), sizeof(Type) * (count), _Alignof(Type)))

#define arena_push_array_zero(arena, Type, count)                                                  \
    ((Type *)arena_alloc_zero((arena), sizeof(Type) * (count), _Alignof(Type)))

#endif
