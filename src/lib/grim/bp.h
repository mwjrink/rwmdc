#pragma once
// #ifndef _BOILERPLATE_H
// #define _BOILERPLATE_H

// clang-format off

#define internal static
#define local_persist static
#define global_var static

#define ro const
#define rw
#define rop(type) type* restrict const
#define rwp(type) type* restrict

#define true 1
#define false 0

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define min(a, b)       ((a) < (b) ? (a) : (b))
#define max(a, b)       ((a) > (b) ? (a) : (b))
#define clamp(a, x, b)  (((x) < (a)) ? (a) : ((b) < (x)) ? (b) : (x))
#define clamp_top(a, b) min(a, b)
#define clamp_bot(a, b) max(a, b)

// TODO is this needed? why not just do a macro at that point?
#define force_inline(ret) inline ret __attribute__((always_inline))

// bit utils
#define msb_32(x) 1U   << (31 - __builtin_clz(x))
#define msb_64(x) 1ULL << (63 - __builtin_clzll(x))
// TODO more technically correct than clz but C23. 
// util that handles 0
// stdc_leading_zeros_ui

// return 1 or -1 or 0
#define sign(x) ((x > 0) - (x < 0))

#define align_up_pow2(x, p)   (((x) + (p) - 1) & ~((p) - 1))
#define align_down_pow2(x, p) ((x) & ~((p) - 1))
#define is_pow2_or_zero(x)    (((x) & ((x) - 1)) == 0)

#define KB(x) ((x) << 10)
#define MB(x) ((x) << 20)
#define GB(x) ((u64)(x) << 30)
#define TB(x) ((u64)(x) << 40llu)

#include <string.h>
#define memory_zero(p, z)       memset((p), 0, (z))
#define memory_zero_struct(p)   memory_zero((p), sizeof(*(p)))
#define memory_zero_array(p)    memory_zero((p), sizeof(p))
#define memory_zero_typed(p, c) memory_zero((p), sizeof(*(p)) * (c))

#define memory_match(a, b, z) (memcmp((a), (b), (z)) == 0)

#define memory_copy(d, s, z)       memmove((d), (s), (z))
#define memory_copy_struct(d, s)   memory_copy((d), (s), min(sizeof(*(d)), sizeof(*(s))))
#define memory_copy_array(d, s)    memory_copy((d), (s), min(sizeof(s), sizeof(d)))
#define memory_copy_typed(d, s, c) memory_copy((d), (s), min(sizeof(*(d)), sizeof(*(s))) * (c))

#define sizeof_member(type, member) (sizeof( ((type *)0)->member ))

// NOTE SHOULD result in cmov or cmovne instructions instead of jumps
// #define setif(cond, val_if_true, val_if_false) ((val_if_false) + ((cond) * ((val_if_true) - (val_if_false))))
// #define setif_u32(cond, dest, src) ((dest) ^ (((src) ^ (dest)) & -(u32)(cond)))
// #define setif(cond, dest, src) ((__builtin_unpredictable(cond)) ? (src) : (dest))

// Helper macros to type-pun float addresses into integer pointers safely
#define __SETIF_PUN32(cond, dest, src) \
    (*(u32*)&(dest) = *(u32*)&(dest) ^ ((*(u32*)&(src) ^ *(u32*)&(dest)) & -(u32)(cond)))

#define __SETIF_PUN64(cond, dest, src) \
    (*(u64*)&(dest) = *(u64*)&(dest) ^ ((*(u64*)&(src) ^ *(u64*)&(dest)) & -(u64)(cond)))

// The master universal branchless macro
#define setif(cond, dest, src) _Generic((dest),               \
    f32: __SETIF_PUN32(cond, dest, src),                      \
    f64: __SETIF_PUN64(cond, dest, src),                      \
    u8:  (dest) = (dest) ^ (((src) ^ (dest)) & -(u8)(cond)),  \
    i8:  (dest) = (dest) ^ (((src) ^ (dest)) & -(i8)(cond)),  \
    u16: (dest) = (dest) ^ (((src) ^ (dest)) & -(u16)(cond)), \
    i16: (dest) = (dest) ^ (((src) ^ (dest)) & -(i16)(cond)), \
    u32: (dest) = (dest) ^ (((src) ^ (dest)) & -(u32)(cond)), \
    i32: (dest) = (dest) ^ (((src) ^ (dest)) & -(i32)(cond)), \
    u64: (dest) = (dest) ^ (((src) ^ (dest)) & -(u64)(cond)), \
    i64: (dest) = (dest) ^ (((src) ^ (dest)) & -(i64)(cond))  \
)

#define cmov32(cond, val_if_true, val_if_false) ((u32)(val_if_false) + ((u32)(cond) * ((u32)(val_if_true) - ((u32)val_if_false))))

#define __branchless_or(mask, v1, v2) (mask & v1) | (~mask & v2);
#define branchless_or(bool32, v1, v2) __branchless_or((-(u32)filled), v1, v2)

// #endif
// https://git.mr4th.com/mr4th-public/mr4th/src/branch/main/src/base/base_context.h
#if defined(__clang__)
# define COMPILER_CLANG 1

# if defined(_WIN32)
#  define OS_WINDOWS 1
# elif defined(__gnu_linux__)
#  define OS_LINUX 1
# elif defined(__APPLE__) && defined(__MACH__)
#  define OS_MAC 1
# else
#  error missing OS detection
# endif

# if defined(__amd64__)
#  define ARCH_X64 1
// TODO(allen): verify this works on clang
# elif defined(__i386__)
#  define ARCH_X86 1
// TODO(allen): verify this works on clang
# elif defined(__arm__)
#  define ARCH_ARM 1
// TODO(allen): verify this works on clang
# elif defined(__aarch64__)
#  define ARCH_ARM64 1
# else
#  error missing ARCH detection
# endif

#elif defined(_MSC_VER)
# define COMPILER_CL 1

# if defined(_WIN32)
#  define OS_WINDOWS 1
# else
#  error missing OS detection
# endif

# if defined(_M_AMD64)
#  define ARCH_X64 1
# elif defined(_M_I86)
#  define ARCH_X86 1
# elif defined(_M_ARM)
#  define ARCH_ARM 1
// TODO(allen): ARM64?
# else
#  error missing ARCH detection
# endif

#elif defined(__GNUC__)
# define COMPILER_GCC 1

# if defined(_WIN32)
#  define OS_WINDOWS 1
# elif defined(__gnu_linux__)
#  define OS_LINUX 1
# elif defined(__APPLE__) && defined(__MACH__)
#  define OS_MAC 1
# else
#  error missing OS detection
# endif

# if defined(__amd64__)
#  define ARCH_X64 1
# elif defined(__i386__)
#  define ARCH_X86 1
# elif defined(__arm__)
#  define ARCH_ARM 1
# elif defined(__aarch64__)
#  define ARCH_ARM64 1
# else
#  error missing ARCH detection
# endif

#else
# error no context cracking for this compiler
#endif

#if !defined(COMPILER_CL)
# define COMPILER_CL 0
#endif
#if !defined(COMPILER_CLANG)
# define COMPILER_CLANG 0
#endif
#if !defined(COMPILER_GCC)
# define COMPILER_GCC 0
#endif
#if !defined(OS_WINDOWS)
# define OS_WINDOWS 0
#endif
#if !defined(OS_LINUX)
# define OS_LINUX 0
#endif
#if !defined(OS_MAC)
# define OS_MAC 0
#endif
#if !defined(ARCH_X64)
# define ARCH_X64 0
#endif
#if !defined(ARCH_X86)
# define ARCH_X86 0
#endif
#if !defined(ARCH_ARM)
# define ARCH_ARM 0
#endif
#if !defined(ARCH_ARM64)
# define ARCH_ARM64 0
#endif

// clang-format on

#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
// typedef uint128_t u128;
// typedef uintptr_t ptr;
// typedef uintptr_t usize;
// apparently using u32 as idx is faster even on 64 bit system?
// typedef uint32_t usize;
#if ARCH_X64 || ARCH_ARM64
typedef u64 usize;
#elif ARCH_x86 || ARCH_ARM
typedef u32 usize;
#endif
typedef u32 uidx;

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float  f32;
typedef double f64;

#if defined(GRIM_ATOMICS)
#include <stdatomic.h>

typedef atomic_uint_least64_t atom_u64;
typedef atomic_int_least64_t  atom_i64;

#endif

#define i8_MIN  (i8)0x80
#define i16_MIN (i16)0x8000
#define i32_MIN (i32)0x80000000
#define i64_MIN (i64)0x8000000000000000llu

#define i8_MAX  (i8)0x7f
#define i16_MAX (i16)0x7fff
#define i32_MAX (i32)0x7fffffff
#define i64_MAX (i64)0x7fffffffffffffffllu

#define u8_MAX  0xff
#define u16_MAX 0xffff
#define u32_MAX 0xffffffff
#define u64_MAX 0xffffffffffffffffllu

#define f32_INF        INFINITY
#define f32_NINF       -INFINITY
#define f32_NAN        NAN
#define f32_EPSILON    1.1920929e-7f
#define f32_PI         3.14159265359f
#define f32_TAU        6.28318530718f
#define f32_E          2.71828182846f
#define f32_GOLD_BIG   1.61803398875f
#define f32_GOLD_SMALL 0.61803398875f

#define f64_INF        INFINITY
#define f64_NINF       -INFINITY
#define f64_NAN        NAN
#define f64_EPSILON    2.220446e-16
#define f64_PI         3.14159265359
#define f64_TAU        6.28318530718
#define f64_E          2.71828182846
#define f64_GOLD_BIG   1.61803398875
#define f64_GOLD_SMALL 0.61803398875
