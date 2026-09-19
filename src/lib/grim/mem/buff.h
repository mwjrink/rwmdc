#pragma once

#include <lib/grim/bp.h>
#include <lib/grim/mem/arena.h>
#include <string.h>

typedef struct RingBuff {
    rop(rw void) data;
    u32 read;
    u32 write;
    u32 cap;
    u32 element_size;
} RingBuff;
STATIC_ASSERT(sizeof(RingBuff) == 24);

typedef struct AllocBuff {
    rop(rw void) data;
    u64 len;
} AllocBuff;
STATIC_ASSERT(sizeof(AllocBuff) == 16);

// #define alloc_buff_init(type, initializer) \
//     type __backer__LINE__[] = initializer; \
//     { \
//         AllocBuff _buff = {0}; \
//         _buff.len = sizeof(__backer__LINE__) / sizeof(type); \
//         _buff.data = &__backer__LINE__; \
//     }

typedef struct ContainedBuff {
    u32 _padding;
    u32 len;
    u8  data[];
} ContainedBuff;
STATIC_ASSERT(sizeof(ContainedBuff) == 8); // Not actually 8 though

// TODO small buffer opt with a union?
typedef struct AllocDynList {
    // TODO These should be u64s
    u32 cap;
    u32 len;
    rwp(rw void) data;

    // TODO this should be a u32?
    usize element_size;
} AllocDynList;
STATIC_ASSERT(sizeof(AllocDynList) == 24);

// #define DEFAULT_CAP 16
//
// AllocDynList alloc_list_create(Arena* arena, usize element_size) {
//     void* data = arena_alloc(arena, DEFAULT_CAP * element_size);
//
//     return (AllocDynList){
//         .cap = DEFAULT_CAP,
//         .len = 0,
//         .data = data,
//         .element_size = element_size,
//     };
// }

RingBuff ring_buff_create_from_scratch_space(rop(rw ScratchArena) scratch, ro u32 element_size) {
    u64 scratch_byte_cap = (u64)msb_64(scratch_free_count(scratch));
    u32 elem_cap         = (u32)(scratch_byte_cap / element_size);
    rop(rw void) data    = scratch_alloc_align(scratch, sizeof(void*), element_size * elem_cap);

    return (RingBuff){
        .data         = data,
        .read         = 0,
        .write        = 0,
        .cap          = elem_cap, // TODO store this as cap-1
        .element_size = element_size,
    };
}

RingBuff ring_buff_create_with_cap(rop(rw Arena) arena, ro u32 element_size, ro u32 pow2_cap) {
    assert(SCOPE_MEM_BUFF, pow2_cap < 16);

    u32 cap           = 1 << pow2_cap;
    rop(rw void) data = arena_alloc_align(arena, sizeof(void*), element_size * cap);

    return (RingBuff){
        .data         = data,
        .read         = 0,
        .write        = 0,
        .cap          = cap, // TODO store this as cap-1
        .element_size = element_size,
    };
}

#define __rb_read(x)    x->data + x->read * x->element_size
#define __rb_write(x)   x->data + x->write * x->element_size
#define __rb_mod(rb, x) ((x) & (rb->cap - 1))

// Returns true if the ring_buffer is full
u32 ring_buff_push(rop(rw RingBuff) ring_buff, rop(ro void) element) {
    assert(SCOPE_MEM_BUFF, ring_buff->read != __rb_mod(ring_buff, ring_buff->write + 1));

    memcpy(__rb_write(ring_buff), element, ring_buff->element_size);

    // % cap if cap = pow2(x) is the same as & cap - 1 (set all bits below cap)
    ring_buff->write = __rb_mod(ring_buff, ring_buff->write + 1);

    return (ring_buff->read == ((ring_buff->write + 1) & (ring_buff->cap - 1)));
}

// Returns true if the ring_buffer is full
u32 ring_buff_push_many(rop(rw RingBuff) ring_buff, rop(ro void) elements, ro u32 elements_count) {
    // can only double+ loop if elements_count > cap
    assert(SCOPE_MEM_BUFF, elements_count < ring_buff->cap);
    assert(SCOPE_MEM_BUFF, ring_buff->read > __rb_mod(ring_buff, ring_buff->write + elements_count));

    memcpy(__rb_write(ring_buff), elements, ring_buff->element_size * elements_count);

    // % cap if cap = pow2(x) is the same as & cap - 1 (set all bits below cap)
    ring_buff->write = __rb_mod(ring_buff, ring_buff->write + elements_count);

    return (ring_buff->read == __rb_mod(ring_buff, ring_buff->write + 1));
}

// Returns true if the ring_buffer is empty
u32 ring_buff_pop(rop(rw RingBuff) ring_buff, rop(rw void) element) {
    assert(SCOPE_MEM_BUFF, ring_buff->read != ring_buff->write);

    memcpy(element, __rb_read(ring_buff), ring_buff->element_size);

    // For easier debugging/error catching
    memset(__rb_read(ring_buff), 0, ring_buff->element_size);

    // % cap if cap = pow2(x) is the same as & cap - 1 (set all bits below cap)
    ring_buff->read = __rb_mod(ring_buff, ring_buff->read + 1);

    return (ring_buff->write == ring_buff->read);
}

// Returns true if the ring_buffer is not full
u32 ring_buff_peek(rop(rw RingBuff) ring_buff, rop(rw void) element) {
    // assert(SCOPE_MEM_BUFF,ring_buff->read != ((ring_buff->write + 1) & (ring_buff->cap - 1)));

    memcpy(element, __rb_read(ring_buff), ring_buff->element_size);

    return (ring_buff->write != ring_buff->read);
}

#undef __rb_read
#undef __rb_write
#undef __rb_mod

AllocDynList alloc_list_create_with_cap(rop(rw Arena) arena, ro usize element_size, ro u32 cap) {
    rop(rw void) data = arena_alloc(arena, cap * element_size);

    return (AllocDynList){
        .cap          = cap,
        .len          = 0,
        .data         = data,
        .element_size = element_size,
    };
}

void* alloc_list_push(rop(rw Arena) arena, rop(rw AllocDynList) list, rop(ro void) element) {
    // DEBUG_LOG("len %u cap %u", list->len, list->cap);
    assert(SCOPE_MEM_BUFF, list->len < list->cap);
    // if (list->len == list->cap) {
    //     // TODO realloc/expand
    // }

    memcpy(list->data + list->len * list->element_size, element, list->element_size);
    void* result = list->data + (list->element_size * list->len);
    list->len += 1;
    return result;
}

void* alloc_list_push_ptr(rop(rw Arena) arena, rop(rw AllocDynList) list, rop(ro void) element) {
    assert(SCOPE_MEM_BUFF, list->element_size == sizeof(void*));
    // if (list->len == list->cap) {
    //     // TODO realloc/expand
    // }

    memcpy(list->data + list->len * list->element_size, (rop(void))(&element), list->element_size);
    void* result = list->data + (list->element_size * list->len);
    list->len += 1;
    return result;
}
