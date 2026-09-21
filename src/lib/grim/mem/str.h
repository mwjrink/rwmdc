#pragma once

#include <lib/grim/bp.h>
#include <lib/grim/mem/arena.h>

// TODO this requires arena alloc etc
// typedef char* Str; // the first 4 bytes are a u32 indicating length

typedef struct Str {
    char* data;
    u32   len;
    struct {
        u32 null_terminated : 1;
        u32 _padding        : 31;
    };
} Str;
STATIC_ASSERT(sizeof(Str) == 16);

#define str_create(string_constant)                                                                                    \
    ((void)sizeof(                                                                                                     \
         struct { int require_literal[_Generic(&(string_constant), char**: -1, const char**: -1, default: 1)]; }),     \
     ((Str){                                                                                                           \
         .data            = (char*)(string_constant),                                                                  \
         .len             = sizeof(string_constant) - 1,                                                               \
         .null_terminated = true,                                                                                      \
     }))

// usage: *_LOG("Print: %.*s", str_fmt(value));
#define str_fmt(s) (i32)(s).len, (s).data

Str str_from_cstring(char* string) {
    u32 len = (u32)strlen(string);
    return (Str){
        .data            = string,
        .len             = len,
        .null_terminated = true,
    };
}

u32 str_equals(Str left, Str right) {
    return (left.len == right.len) && memcmp(left.data, right.data, right.len) == 0;
}

u32 str_starts_with(Str value, Str prefix) {
    return (value.len >= prefix.len) && memcmp(value.data, prefix.data, prefix.len) == 0;
}

u32 str_ends_with(Str value, Str suffix) {
    return (value.len >= suffix.len) && memcmp(value.data + value.len - suffix.len, suffix.data, suffix.len) == 0;
}
