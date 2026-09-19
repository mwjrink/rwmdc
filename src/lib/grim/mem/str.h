#pragma once

#include <lib/grim/bp.h>
#include <lib/grim/mem/arena.h>

// TODO this requires arena alloc etc
// typedef char* Str; // the first 4 bytes are a u32 indicating length

typedef struct Str {
    char* data;
    u32   len;
    u32   _padding;
} Str;
STATIC_ASSERT(sizeof(Str) == 16);

// TODO is there a way to only store this static string once? does the compiler get rid of it?
#define create_string(string_constant)                                                                                 \
    _Generic((x),                                                                                                      \
        char*: STATIC_ASSERT(0),                                                                                       \
        const char*: STATIC_ASSERT(0),                                                                                 \
        char[sizeof(x)]: ((String){.data = (char*)&string_constant, .len = sizeof(string_constant) - 1}))

#define str_fmt(s) (i32)(s).len, (s).data

Str create_string_from_non_const(char* string) {
    u32 len = (u32)strlen(string);
    return (Str){
        .data = string,
        .len  = len,
    };
}
