#pragma once

#include <lib/grim/assert.h>
#include <lib/grim/bp.h>

typedef struct String {
    u8* data;
    u32 len;
    u32 _padding;
} String;
STATIC_ASSERT(sizeof(String) == 16);
