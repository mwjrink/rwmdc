#pragma once

#include <lib/grim/bp.h>
#include <stdio.h>

#undef assert
#define STATIC_ASSERT(condition) _Static_assert((condition), #condition)
#define assert(scope, condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__, #condition); \
        abort(); \
    } \
} while (0)
