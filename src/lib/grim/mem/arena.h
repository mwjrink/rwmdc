#pragma once

// #ifndef _ARENA_IMPL_H
// #define _ARENA_IMPL_H

#include <lib/grim/assert.h>
#include <lib/grim/bp.h>
#include <stdalign.h>
#include <string.h>
#include <sys/mman.h> /* mmap() is defined in this header */

// #include <unistd.h>
#include <errno.h>

// #define ARENA_VCAP TB(1)
#define ARENA_VCAP GB(1)
#define PAGE_SIZE  GB(1)

#define DEBUG_DUMP_ARENA(arena)                                                                                        \
    DEBUG_LOG(SCOPE_MEM_ARENA,                                                                                         \
              "%s arena:\n    len: %lu\n    data: %p\n    ckpt: %p\n",                                                 \
              __func__,                                                                                                \
              arena->len,                                                                                              \
              arena->data,                                                                                             \
              arena->ckpt);

global_var u32 huge_pages_supported = true;

// ARG ORDER IS DIFFERENT ON LINUX
rop(rw void) alloc_page() {
    rwp(rw void) data = MAP_FAILED;

    if (huge_pages_supported == true) {
        data = mmap(0,
                    ARENA_VCAP,
                    // PROT_NONE,
                    PROT_READ | PROT_WRITE,
                    // MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_HUGETLB | MAP_HUGE_1GB,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_1GB,
                    -1,
                    0); //
    }

    if (data == MAP_FAILED) {
        WARNING_LOG(SCOPE_MEM_ARENA,
                    "Huge pages are not supported. Please ensure they are setup correctly for better performance.");
        huge_pages_supported = false;

        data = mmap(0,
                    ARENA_VCAP,
                    // PROT_NONE,
                    PROT_READ | PROT_WRITE,
                    // MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                    MAP_PRIVATE | MAP_ANONYMOUS,
                    -1,
                    0); //
        madvise(data, PAGE_SIZE, MADV_HUGEPAGE);
    }

    if (data == MAP_FAILED) {
        CRITICAL_LOG(SCOPE_MEM_ARENA, "Failed to allocate an arena page.");
        CRITICAL_LOG(SCOPE_MEM_ARENA, "errno: %u", errno);

        // clang-format off
       //  switch (errno) {
       //
       // EACCES A file descriptor refers to a non-regular file.  Or a file mapping was requested, but fd is not open for reading.  Or MAP_SHARED was requested and PROT_WRITE is set, but fd is not open in read/write (O_RDWR) mode.  Or PROT_WRITE is set, but the file is append-only.
       // EAGAIN The file has been locked, or too much memory has been locked (see setrlimit(2)).
       // EBADF  fd is not a valid file descriptor (and MAP_ANONYMOUS was not set).
       // EEXIST MAP_FIXED_NOREPLACE was specified in flags, and the range covered by addr and length clashes with an existing mapping.
       // EINVAL We don't like addr, length, or offset (e.g., they are too large, or not aligned on a page boundary).
       // EINVAL (since Linux 2.6.12) length was 0.
       // EINVAL flags contained none of MAP_PRIVATE, MAP_SHARED, or MAP_SHARED_VALIDATE.
       // ENFILE The system-wide limit on the total number of open files has been reached.
       // ENODEV The underlying filesystem of the specified file does not support memory mapping.
       // ENOMEM No memory is available.
       // ENOMEM The process's maximum number of mappings would have been exceeded.  This error can also occur for munmap(), when unmapping a region in the middle of an existing mapping, since this results in two smaller mappings on either side of the region being unmapped.
       // ENOMEM (since Linux 4.7) The process's RLIMIT_DATA limit, described in getrlimit(2), would have been exceeded.
       // ENOMEM We don't like addr, because it exceeds the virtual address space of the CPU.
       // EOVERFLOW On 32-bit architecture together with the large file extension (i.e., using 64-bit off_t): the number of pages used for length plus number of pages used for offset would overflow unsigned long (32 bits).
       // EPERM  The prot argument asks for PROT_EXEC but the mapped area belongs to a file on a filesystem that was mounted no-exec.
       // EPERM  The operation was prevented by a file seal; see fcntl(2).
       // EPERM The MAP_HUGETLB flag was specified, but the caller was not privileged(did not have the CAP_IPC_LOCK capability) and is not a member of the hugetlb_shm_group group; see the description of /proc/sys/vm/hugetlb_shm_group in proc_sys_vm(5).
       // ETXTBSY MAP_DENYWRITE was set but the object specified by fd is open for writing.
       //  }
        // clang-format on
        exit(1);
    }

    // THE ADV CONFLICT WITH EACH-OTHER // madvise(data, PAGE_SIZE, MADV_HUGEPAGE | MADV_WILLNEED | MADV_SEQUENTIAL);
    return data;
}

// TODO dump assets into .o files and link them? Ryan Fleury mentioned this in wookash vs Ginger Bill
// better way of embedding assets into an executable

typedef struct Arena {
    usize len;
    rwp(rw void) data;
    rwp(rw void) ckpt; // use a u32 index instead of a pointer?
} Arena;
STATIC_ASSERT(sizeof(Arena) == 24);

typedef struct ScratchArena {
    usize len;
    rwp(rw void) data;
} ScratchArena;
STATIC_ASSERT(sizeof(ScratchArena) == 16);

// FUTURE allow custom page size.
//     This requires custom checks in alloc etc for size/cap
Arena arena_create() {
    rop(rw void) data = alloc_page();
    // i32 res1        = mprotect(data, PAGE_SIZE, PROT_READ | PROT_WRITE);
    // i32 res2        = madvise(data, PAGE_SIZE, MADV_WILLNEED);
    // DEBUG_LOG(SCOPE_MEM_ARENA, "res: %i, %i", res1, res2);

    // posix_memalign();

    // instead of mmap?
    // void* current_ptr = sbrk(0);
    // void* new_mem = sbrk(100);
    // if (new_mem == (void*)-1) {
    //    // FAIL
    // }
    // brk(current_ptr); // recall to a previous block

    Arena arena = {
        .ckpt = 0,
        .data = (data + sizeof(Arena)),
        .len  = 0,
    };

    *(Arena*)data = arena;

    return arena;
}

ScratchArena scratch_create() {
    rop(rw void) data = alloc_page();
    // mprotect(data, PAGE_SIZE, PROT_READ | PROT_WRITE);
    // madvise(data, PAGE_SIZE, MADV_WILLNEED);

    ScratchArena scratch = {
        .data = data,
        .len  = 0,
    };

    return scratch;
}

// checkpoint
// TODO there is a bug if you have full or nearly full arena and try to make a checkpoint.
// Also check and silently disallow consecutive checkpoints??
// Actually... I should let them. It keeps the code consistent for start and finish and runtime branching
void arena_ckpt(rop(rw Arena) arena) {
    rop(rw void) new_ckpt = arena->data + arena->len;
    *(void**)new_ckpt     = arena->ckpt;
    arena->len += sizeof(void*);
    arena->ckpt = new_ckpt;
}

void arena_rollback(rop(rw Arena) arena) {
#if defined(__DEBUG)
    memset(arena->ckpt + sizeof(void*), 0, (arena->data + arena->len) - (arena->ckpt + sizeof(void*)));
#endif

    arena->len = sizeof(void*) + arena->ckpt - arena->data;
}

void arena_pop(rop(rw Arena) arena) {
    void* old_ckpt = *(void**)arena->ckpt;

#if defined(__DEBUG)
    memset(arena->ckpt, 0, (arena->data + arena->len) - arena->ckpt);
#endif

    arena->len  = arena->ckpt - arena->data;
    arena->ckpt = old_ckpt;
}

void scratch_pop_to(rop(rw ScratchArena) scratch, void* to) {
    u64 new_len = ((void*)to - scratch->data);
#if defined(__DEBUG)
    memset(to, 0, scratch->len - new_len);
#endif

    scratch->len = new_len;
}

void scratch_reset(rop(rw ScratchArena) scratch) {
#if defined(__DEBUG)
    memset(scratch->data, 0, scratch->len);
#endif

    scratch->len = 0;
}

void arena_destroy(rop(rw Arena) arena) {
    munmap(arena->data - sizeof(Arena), PAGE_SIZE);
    arena->len  = PAGE_SIZE;
    arena->data = NULL;
}

void scratch_destroy(rop(rw ScratchArena) scratch) {
    munmap(scratch->data, PAGE_SIZE);
    scratch->len  = PAGE_SIZE;
    scratch->data = NULL;
}

// pop and zero the freed space
// void arena_pop_zero(Arena* arena) {
//     if (arena->ckpt == 0) {
//         Arena old_page = *(Arena*)(arena->data - sizeof(Arena));
//
//         if (arena->data == old_page.data) {
//             *arena = old_page;
//             return;
//         }
//
//         munmap(arena->data - sizeof(Arena), PAGE_SIZE);
//         *arena = old_page;
//         return;
//     }
//
//     memset(arena->ckpt, 0, arena->data + arena->len - arena->ckpt);
//     arena->len = arena->ckpt - arena->data;
//     arena->ckpt = *(void**)arena->ckpt;
// }

// void arena_force_new_page(rop(rw Arena) arena) {
//     exit(1);
//     void* new_page = alloc_page();
//     // printf("new page created: %p\n", new_page);
//
//     *(Arena*)new_page = *arena;
//     arena->data = new_page + sizeof(Arena);
//     arena->len = 0;
//     arena->ckpt = 0;
// }

void* arena_dyn_start(rop(rw Arena) arena) {
    return arena->data + arena->len;
}

void* arena_dyn_start_align(rop(rw Arena) arena, usize align) {
    assert(SCOPE_MEM_ARENA, is_pow2_or_zero(align));

    usize partial = align - arena->len & (align - 1);
    return arena->data + arena->len + partial;
}

// TODO should this take a new pointer?
void arena_dyn_end(rop(rw Arena) arena, void* end) {
    arena->len = end - arena->data;
}

void* scratch_dyn_start(rop(rw ScratchArena) scratch) {
    return scratch->data + scratch->len;
}

// TODO need a way to ensure I don't overflow the scratch space here, right?
void* scratch_dyn_start_align(rop(rw ScratchArena) scratch, usize align) {
    assert(SCOPE_MEM_ARENA, is_pow2_or_zero(align));

    usize partial = align - scratch->len & (align - 1);
    return scratch->data + scratch->len + partial;
}

// TODO should this take a new pointer?
void scratch_dyn_end(rop(rw ScratchArena) scratch, void* end) {
    scratch->len = end - scratch->data;
}

u64 scratch_free_count(rop(rw ScratchArena) scratch) {
    // TODO probably store cap in the arena?
    return (ARENA_VCAP - scratch->len);
}

void* arena_alloc(rop(rw Arena) arena, usize size) {
    assert(SCOPE_MEM_ARENA, size < PAGE_SIZE);

    void* result = arena->data + arena->len;
    arena->len += size;

    memset(result, 0, size);

    return result;
}

void* scratch_alloc(rop(rw ScratchArena) scratch, usize size) {
    assert(SCOPE_MEM_ARENA, size < PAGE_SIZE);

    void* result = scratch->data + scratch->len;
    scratch->len += size;

    memset(result, 0, size);

    return result;
}

// TODO refactor everything to use this macro, move the align import here
#define arena_alloc_aligned(arena, type, count) arena_alloc_align(arena, alignof(type), sizeof(type) * count)
void* arena_alloc_align(rop(rw Arena) arena, usize align, usize size) {
    assert(SCOPE_MEM_ARENA, size < PAGE_SIZE);
    assert(SCOPE_MEM_ARENA, is_pow2_or_zero(align));

    usize partial = align - arena->len & (align - 1);
    if (partial != 0) {
        DEBUG_LOG(SCOPE_MEM_ARENA, "arena partial %lu", partial);
    }
    void* result = arena->data + arena->len + partial;

    memset(arena->data + arena->len, 0, size + partial);

    arena->len += size + partial;

    return result;
}

#define scratch_alloc_aligned(scratch, type, count) scratch_alloc_align(scratch, alignof(type), sizeof(type) * count)
void* scratch_alloc_align(rop(rw ScratchArena) scratch, usize align, usize size) {
    assert(SCOPE_MEM_ARENA, size < PAGE_SIZE);
    assert(SCOPE_MEM_ARENA, is_pow2_or_zero(align));

    usize partial = align - scratch->len & (align - 1);
    if (partial != 0) {
        DEBUG_LOG(SCOPE_MEM_ARENA, "scratch partial %lu", partial);
    }
    void* result = scratch->data + scratch->len + partial;

    memset(scratch->data + scratch->len, 0, size + partial);

    scratch->len += size + partial;

    return result;
}

// return the data pointer and do nothing.
// If you call anything else on arena after
// calling this and before calling post, UB
// void* arena_pre_alloc(Arena* arena) { return arena->data + arena->len; }

// After calling pre and writing some data to the arena,
// tell me how much data you wrote.
// TODO a post_pop? where you don't care about the mem?
// void arena_post_alloc(Arena* arena, usize size) {
//     assert(SCOPE_MEM_ARENA,size < PAGE_SIZE - arena->len);
//     arena->len += size;
// }

char* arena_copy_str(Arena* arena, char* str) {
    void* dst     = arena->data + arena->len;
    void* new_loc = memccpy(dst, str, '\0', PAGE_SIZE - arena->len);
    arena->len    = new_loc - arena->data;
    return dst;
}

char* scratch_copy_str(ScratchArena* scratch, char* str) {
    void* dst     = scratch->data + scratch->len;
    void* new_loc = memccpy(dst, str, '\0', PAGE_SIZE - scratch->len);
    scratch->len  = new_loc - scratch->data;
    return dst;
}

#ifdef TEST

#include <stdio.h>
#include <unistd.h> // Required for sleep()
void arena_test() {
    printf("Starting Arena Alloc Tests!\n");
    Arena arena = arena_create();

    printf("first page: %p\n", arena.data);

    printf("Allocating 200 x 4096 * 1024\n");
    u8* final;
    for (int i = 0; i < 200; i++) {
        final  = (u8*)arena_alloc(&arena, 4096 * 1024);
        *final = 0xFF;
    }

    printf("Creating Checkpoint\n");
    arena_ckpt(&arena);

    printf("Allocating 55 x 4096\n");
    for (int i = 0; i < 55; i++) {
        final  = (u8*)arena_alloc(&arena, 4096 * 1024);
        *final = 0xFF;
    }

    printf("Rolling Back\n");
    arena_rollback(&arena);

    printf("Allocating 55 x 4096\n");
    for (int i = 0; i < 55; i++) {
        final  = (u8*)arena_alloc(&arena, 4096 * 1024);
        *final = 0xFF;
    }

    printf("Creating Checkpoint\n");
    arena_ckpt(&arena);

    printf("Writing to final ptr\n");
    for (int i = 0; i < 4096; i++) {
        final[i] = 0xFF;
    }

    printf("Allocating 1 x 4096 * 20\n");
    u8* new_page = arena_alloc(&arena, 4096 * 1024 * 20);

    for (int i = 0; i < 10000; i++) {
        final  = (u8*)arena_alloc(&arena, 4096 * 1024 * 25);
        *final = 0xFF;
    }
    sleep(5); // Pause execution for 5 seconds

    printf("second page: %p\n", arena.data);

    printf("Writing to new_page ptr\n");
    for (int i = 0; i < 40960; i++) {
        new_page[i] = 0xFF;
    }

    printf("Creating Checkpoint\n");
    arena_ckpt(&arena);

    printf("Allocating 3 x 40960\n");
    new_page  = arena_alloc(&arena, 4096 * 1024 * 10);
    *new_page = 0xFF;
    new_page  = arena_alloc(&arena, 4096 * 1024 * 10);
    *new_page = 0xFF;
    new_page  = arena_alloc(&arena, 4096 * 1024 * 10);
    *new_page = 0xFF;

    printf("Rolling Back\n");
    arena_rollback(&arena);

    printf("Rolling Back to Previous Page\n");
    arena_rollback(&arena);

    printf("Rolling Back 5 times\n");
    printf("on page: %p\n", arena.data);
    printf("1\n");
    arena_rollback(&arena);
    printf("on page: %p\n", arena.data);
    printf("2\n");
    arena_rollback(&arena);
    printf("on page: %p\n", arena.data);
    printf("3\n");
    arena_rollback(&arena);
    printf("on page: %p\n", arena.data);
    printf("4\n");
    arena_rollback(&arena);
    printf("on page: %p\n", arena.data);
    printf("5\n");
    arena_rollback(&arena);
    printf("on page: %p\n", arena.data);

    printf("Done!\n");
}

#endif

// #endif
