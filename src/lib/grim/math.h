#pragma once

#include <lib/grim/assert.h>
#include <lib/grim/bp.h>
#include <math.h>

typedef struct Vec2 { f32 x, y; } Vec2;
typedef struct Vec4 { f32 x, y, z, w; } Vec4;
STATIC_ASSERT(sizeof(Vec2) == 8);
STATIC_ASSERT(sizeof(Vec4) == 16);
