#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define internal static
#define local_persist static
#define global_var static
#define ro const
#define rw
#define rop(type) type *restrict const
#define rwp(type) type *restrict
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef size_t usize;
typedef float f32;
typedef double f64;

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define clamp(a, x, b) ((x) < (a) ? (a) : (x) > (b) ? (b) : (x))
#define align_up_pow2(x, p) (((x) + (p) - 1) & ~((p) - 1))
#define is_pow2_or_zero(x) (((x) & ((x) - 1)) == 0)
#define KB(x) ((u64)(x) << 10)
#define MB(x) ((u64)(x) << 20)
#define GB(x) ((u64)(x) << 30)
#define u8_MAX UINT8_MAX
#define u16_MAX UINT16_MAX
#define u32_MAX UINT32_MAX
#define u64_MAX UINT64_MAX
#define i16_MIN INT16_MIN
#define i16_MAX INT16_MAX
#define i32_MIN INT32_MIN
#define i32_MAX INT32_MAX

#define memory_zero(p, z) memset((p), 0, (z))
#define memory_zero_struct(p) memory_zero((p), sizeof(*(p)))
#define memory_zero_array(p) memory_zero((p), sizeof(p))
#define memory_zero_typed(p, c) memory_zero((p), sizeof(*(p)) * (c))
#define memory_copy(d, s, z) memmove((d), (s), (z))
#define memory_copy_struct(d, s) memory_copy((d), (s), sizeof(*(d)))
#define memory_copy_typed(d, s, c) memory_copy((d), (s), sizeof(*(d)) * (c))
