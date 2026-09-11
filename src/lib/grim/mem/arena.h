#pragma once

#include <lib/grim/assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <sys/mman.h>

// Reserve virtual address space once; pages become resident only when touched.
#define ARENA_CAPACITY ((usize)128 * 1024 * 1024)

typedef struct Arena {
    usize len;
    void *data;
    void *ckpt;
    usize peak;
} Arena;

typedef struct ArenaCheckpoint {
    void *previous;
    usize length;
} ArenaCheckpoint;

internal Arena arena_create(void) {
    void *data = mmap(NULL, ARENA_CAPACITY, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (data == MAP_FAILED) { perror("mmap arena"); exit(1); }
    return (Arena){.data=data};
}

internal void *arena_alloc_align(Arena *arena, usize alignment, usize size) {
    assert(SCOPE_MEM_ARENA, alignment && is_pow2_or_zero(alignment));
    usize padding = (0 - ((uintptr_t)arena->data + arena->len)) & (alignment - 1);
    if (arena->len > ARENA_CAPACITY || padding > ARENA_CAPACITY - arena->len ||
        size > ARENA_CAPACITY - arena->len - padding) {
        fprintf(stderr, "Arena capacity exceeded (used=%zu, requested=%zu, capacity=%zu)\n",
                arena->len, size, ARENA_CAPACITY);
        exit(1);
    }
    void *result = (u8 *)arena->data + arena->len + padding;
    arena->len += padding + size;
    if (arena->len > arena->peak) arena->peak = arena->len;
    memset(result, 0, size);
    return result;
}

internal void *arena_alloc(Arena *arena, usize size) {
    return arena_alloc_align(arena, alignof(max_align_t), size);
}

internal void *arena_alloc_array(Arena *arena, usize alignment, usize element_size, usize count) {
    if (count > SIZE_MAX / element_size) { fprintf(stderr, "Arena array size overflow\n"); exit(1); }
    return arena_alloc_align(arena, alignment, element_size * count);
}
#define arena_alloc_aligned(arena, type, count) \
    arena_alloc_array((arena), alignof(type), sizeof(type), (count))

internal void arena_ckpt(Arena *arena) {
    usize length = arena->len;
    ArenaCheckpoint *checkpoint = arena_alloc_aligned(arena, ArenaCheckpoint, 1);
    checkpoint->previous = arena->ckpt;
    checkpoint->length = length;
    arena->ckpt = checkpoint;
}

internal void arena_rollback(Arena *arena) {
    assert(SCOPE_MEM_ARENA, arena->ckpt != NULL);
    arena->len = (usize)((u8 *)arena->ckpt - (u8 *)arena->data) + sizeof(ArenaCheckpoint);
}

internal void arena_pop(Arena *arena) {
    assert(SCOPE_MEM_ARENA, arena->ckpt != NULL);
    ArenaCheckpoint checkpoint = *(ArenaCheckpoint *)arena->ckpt;
    arena->len = checkpoint.length;
    arena->ckpt = checkpoint.previous;
}

internal void arena_destroy(Arena *arena) {
    if (arena->data) munmap(arena->data, ARENA_CAPACITY);
    *arena = (Arena){0};
}
