#pragma once

#ifndef LOGGER_H
#define LOGGER_H

#include <bits/time.h>
#include <lib/grim/bp.h>
#include <stdarg.h>
#include <stdio.h>

// #ifdef linux
#define _GNU_SOURCE
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>

#define PERF_CYCLES 0
#define PERF_NANOS  1

#ifdef __x86_64
#include <cpuid.h>
#include <x86intrin.h>

// TODO technically these macros are wrong, this is linux only right now
u64 _clock_nano();

#if PERF_NANOS
#define get_clocks() _clock_nano() // invariant per core
#elif PERF_CYCLES
// TODO __rdpmc read cache misses etc
// TODO do I care about invariant time?
// #define get_clocks() __rdtsc() // per core diff
#define get_clocks() __rdtscp()    // invariant per core
#endif

// #define cpi_info(x)  __cpuid(x)
#elif defined(__aarch64__)
/*
 * According to ARM DDI 0487F.c, from Armv8.0 to Armv8.5 inclusive, the
 * system counter is at least 56 bits wide; from Armv8.6, the counter
 * must be 64 bits wide.  So the system counter could be less than 64
 * bits wide and it is attributed with the flag 'cap_user_time_short'
 * is true.
 */
#define get_clocks()                                                                                                   \
    ({                                                                                                                 \
        u64 val;                                                                                                       \
        asm volatile("mrs %0, cntvct_el0" : "=r"(val));                                                                \
        (unsigned long long)val;                                                                                       \
    })

#else
#include <time.h>
// cast for compat with __rdtsc()
#define get_clocks() (unsigned long long)clock()
#endif

#define A_START       "\e["
#define A_BOLD        "1"
#define A_FG(r, g, b) "38;2;" #r ";" #g ";" #b
#define A_BG(r, g, b) "48;2;" #r ";" #g ";" #b
#define A_RESET       "\e[0m"
#define A_END         "m"

global_var u64 verbose_log_count;
global_var u64 info_log_count;
global_var u64 debug_log_count;
global_var u64 warning_log_count;
global_var u64 error_log_count;
global_var u64 critical_log_count;

// =================================================================================================================
// =                                                        SCOPES                                                 =
// =================================================================================================================

// static const i32 SCOPE_DISABLED_VALUE = __COUNTER__;

#define EXTRACT_LEVEL(scope_val) (((scope_val) >> 24) & 0xFF)
#define EXTRACT_ID(scope_val)    ((scope_val) & 0x00FFFFFF)

typedef enum Severity {
    LEVEL_DISABLED = 0,
    LEVEL_CRITICAL = 1,
    LEVEL_ERROR    = 2,
    LEVEL_WARNING  = 3,
    LEVEL_DEBUG    = 4,
    LEVEL_INFO     = 5,
    LEVEL_VERBOSE  = 6,
    LEVEL_ALL      = 255,
} Severity;

// Master List: Scope Name | Max Allowed Log Level for this scope
#define ALL_SCOPES(X)                                                                                                  \
    X(DISABLED_VALUE, LEVEL_DISABLED)                                                                                  \
                                                                                                                       \
    X(PHYSICS, LEVEL_ALL)                                                                                              \
                                                                                                                       \
    X(STARTUP, LEVEL_WARNING)                                                                                          \
    X(SHUTDOWN, LEVEL_WARNING)                                                                                         \
                                                                                                                       \
    X(DEBUG, LEVEL_ALL)                                                                                                \
                                                                                                                       \
    X(STBI, LEVEL_WARNING)                                                                                             \
                                                                                                                       \
    X(X11, LEVEL_WARNING)                                                                                              \
    X(WAYLAND, LEVEL_WARNING)                                                                                          \
                                                                                                                       \
    X(MATH, LEVEL_WARNING)                                                                                             \
    X(CAMERA, LEVEL_WARNING)                                                                                           \
    X(LOAD, LEVEL_WARNING)                                                                                             \
                                                                                                                       \
    X(FONT_LOAD, LEVEL_WARNING)                                                                                        \
    X(CONFIG, LEVEL_WARNING)                                                                                           \
                                                                                                                       \
    X(INPUT_LINUX, LEVEL_WARNING)                                                                                      \
    X(PERF_LINUX, LEVEL_WARNING)                                                                                       \
                                                                                                                       \
    X(RUNE_REPO, LEVEL_WARNING)                                                                                        \
    X(RUNE_READER, LEVEL_WARNING)                                                                                      \
    X(RUNE_WRITER, LEVEL_WARNING)                                                                                      \
                                                                                                                       \
    X(MEM_ARENA, LEVEL_WARNING)                                                                                        \
    X(MEM_BUFF, LEVEL_WARNING)                                                                                         \
                                                                                                                       \
    X(GFX_DEBUG, LEVEL_WARNING)                                                                                        \
    X(GFX_INIT, LEVEL_WARNING)                                                                                         \
    X(GFX_SWAPCHAIN, LEVEL_WARNING)                                                                                    \
    X(GFX_VALIDATION, LEVEL_ALL)                                                                                       \
    X(GFX_BUFFER, LEVEL_WARNING)                                                                                       \
    X(GFX_AFTERMATH, LEVEL_WARNING)                                                                                    \
    X(GFX_PIPELINE, LEVEL_WARNING)                                                                                     \
    X(GFX_COMMAND_BUFFER, LEVEL_WARNING)                                                                               \
    X(GFX_COMMANDS, LEVEL_WARNING)                                                                                     \
    X(GFX_DESCRIPTORS, LEVEL_WARNING)                                                                                  \
    X(GFX_IMAGE, LEVEL_WARNING)                                                                                        \
    X(GFX_EXT, LEVEL_WARNING)                                                                                          \
    X(GFX_PRESENT, LEVEL_WARNING)                                                                                      \
    X(GFX_SHADER, LEVEL_WARNING)                                                                                       \
    X(GFX_COMMAND_QUEUE, LEVEL_WARNING)                                                                                \
                                                                                                                       \
    X(SPAN, LEVEL_WARNING)

// clang-format off
typedef enum {
#define X(name, level) SCOPE_##name = ((level << 24) | __COUNTER__),
    ALL_SCOPES(X)
#undef X
} LogScope;
// clang-format on

// TODO find a way to get scope names into an array?

static char* SCOPE_NAMES[] = {
#define X(name, level) #name,
    ALL_SCOPES(X)
#undef X
};

// =================================================================================================================

// printf(ANSI_ESCAPE_START ANSI_BOLD ANSI_COLOR_BACKGROUND(255, 127, 127) ANSI_ESCAPE_END
// "Background Red\n" ANSI_RESET);
// printf(ANSI_ESCAPE_START ANSI_BOLD ANSI_COLOR_FOREGROUND(0, 255, 127) ANSI_COLOR_BACKGROUND(255, 127, 127)
// ANSI_ESCAPE_END "Background Red & Foreground Green\n" ANSI_RESET);

// printf("\e[1;38;2;23;147;209mCachyOS \e[0mBlue\n");
// printf("\e[1;48;2;23;147;209mCachyOS \e[0mBlue\n");
// sleep(5);
void print_log_summary() {
    const char* line = "+--------------+--------------+";

    printf("\n%s\n", line);
    printf("| Severity     | Count        |\n");
    printf("%s\n", line);

    // VERBOSE
    printf("| " A_START A_BOLD ";" A_BG(64, 64, 64) ";" A_FG(200, 200, 200) A_END);
    printf(" VERBOSE    " A_RESET " | %12lu |\n", verbose_log_count);

    // INFO
    printf("| " A_START A_BOLD ";" A_BG(5, 200, 255) ";" A_FG(255, 255, 255) A_END);
    printf(" INFO       " A_RESET " | %12lu |\n", info_log_count);

    // DEBUG
    printf("| " A_START A_BOLD ";" A_BG(50, 50, 255) ";" A_FG(255, 255, 255) A_END);
    printf(" DEBUG      " A_RESET " | %12lu |\n", debug_log_count);

    // WARNING
    printf("| " A_START A_BOLD ";" A_BG(200, 150, 0) ";" A_FG(255, 255, 255) A_END);
    printf(" WARNING    " A_RESET " | %12lu |\n", warning_log_count);

    // ERROR
    printf("| " A_START A_BOLD ";" A_BG(200, 0, 0) ";" A_FG(255, 255, 255) A_END);
    printf(" ERROR      " A_RESET " | %12lu |\n", error_log_count);

    // CRITICAL
    printf("| " A_START A_BOLD ";" A_BG(255, 255, 255) ";" A_FG(200, 0, 0) A_END);
    printf(" CRITICAL   " A_RESET " | %12lu |\n", critical_log_count);

    printf("%s\n\n", line);
}

void __attribute__((format(printf, 5, 6))) _log(
    const Severity severity, const LogScope scope, const char* file, int line, const char* restrict fmt, ...) {

    va_list args;

    switch (severity) {
        case LEVEL_VERBOSE: {
            verbose_log_count += 1;

#ifdef LOG_LEVEL_VERBOSE
            printf(A_START);

            va_start(args, fmt);

            printf(A_BOLD ";" A_BG(64, 64, 64) ";" A_FG(200, 200, 200));
            printf(A_END);
            printf(" VERBOSE  " A_RESET);
#else
            return;
#endif
        } break;
        case LEVEL_INFO: {
            info_log_count += 1;
#ifdef LOG_LEVEL_INFO
            printf(A_START);

            va_start(args, fmt);

            printf(A_BOLD ";" A_BG(5, 200, 255) ";" A_FG(255, 255, 255));
            printf(A_END);
            printf(" INFO     " A_RESET);
#else
            return;
#endif
        } break;
        case LEVEL_DEBUG: {
            debug_log_count += 1;
#ifdef LOG_LEVEL_DEBUG
            printf(A_START);

            va_start(args, fmt);

            printf(A_BOLD ";" A_BG(50, 50, 255) ";" A_FG(255, 255, 255));
            printf(A_END);
            printf(" DEBUG    " A_RESET);
#else
            return;
#endif
        } break;
        case LEVEL_WARNING: {
            warning_log_count += 1;
#ifdef LOG_LEVEL_WARNING
            printf(A_START);

            va_start(args, fmt);

            printf(A_BOLD ";" A_BG(200, 150, 0) ";" A_FG(255, 255, 255));
            printf(A_END);
            printf(" WARNING  " A_RESET);
#else
            return;
#endif
        } break;
        case LEVEL_ERROR: {
            error_log_count += 1;
#ifdef LOG_LEVEL_ERROR
            printf(A_START);

            va_start(args, fmt);

            printf(A_BOLD ";" A_BG(200, 0, 0) ";" A_FG(255, 255, 255));
            printf(A_END);
            printf(" ERROR    " A_RESET);
#else
            return;
#endif
        } break;
        case LEVEL_CRITICAL: {
            critical_log_count += 1;
#ifdef LOG_LEVEL_CRITICAL
            printf(A_START);

            va_start(args, fmt);

            printf(A_BOLD ";" A_BG(255, 255, 255) ";" A_FG(200, 0, 0));
            printf(A_END);
            printf(" CRITICAL " A_RESET);
#else
            return;
#endif
        } break;
        default: {
        } break;
    }

    printf(" | " A_START A_BG(160, 124, 196) ";" A_FG(31, 12, 0) ";" A_BOLD A_END " %s:%i " A_RESET " | ", file, line);

#ifdef linux
    long thread_id = syscall(SYS_gettid);
    // pid_t thread_id = gettid();
#elif defined(win32)
    DWORD thread_id = GetCurrentThreadId();
#endif

#ifdef __x86_64
    u32                core_id   = -1;
    unsigned long long timestamp = __rdtscp(&core_id);
    printf(A_START A_BG(255, 130, 218) ";" A_FG(31, 12, 0) ";" A_BOLD A_END " %llu clocks " A_RESET " | ", timestamp);
    printf(A_START A_BG(50, 168, 82) ";" A_FG(31, 12, 0) ";" A_BOLD A_END
                                                         " THREAD: %ld on CORE: %02u with SCOPE: %s" A_RESET " | ",
           thread_id,
           core_id,
           SCOPE_NAMES[EXTRACT_ID(scope)]);
#elif defined(__aarch64__)
/*
 * According to ARM DDI 0487F.c, from Armv8.0 to Armv8.5 inclusive, the
 * system counter is at least 56 bits wide; from Armv8.6, the counter
 * must be 64 bits wide.  So the system counter could be less than 64
 * bits wide and it is attributed with the flag 'cap_user_time_short'
 * is true.
 */
#define get_clocks()                                                                                                   \
    u64 val;                                                                                                           \
    asm volatile("mrs %0, cntvct_el0" : "=r"(val));

    printf(A_START A_BG(255, 130, 218) ";" A_FG(31, 12, 0) ";" A_BOLD A_END " %llu clocks " A_RESET " | ",
           (unsigned long long)val);
#else
#include <time.h>
    // cast for compat with __rdtsc()
    printf(A_START A_BG(255, 130, 218) ";" A_FG(31, 12, 0) ";" A_BOLD A_END " %llu clocks " A_RESET " | ",
           (unsigned long long)clock());
#endif

    vprintf(fmt, args);

    va_end(args);

    printf("\n");
}

// __DATE__ for compile date, __TIME__ for compile time
// __STDC_VERSION__ for c strandard version

// TODO ADD LOG SCOPE AND FILTER FOR DEBUGGING SPECIFIC THINGS SO INFO ISN'T PRINTING EVERYTHING

// #define _RAW_LOG(sev, scope, file, line, ...)
// #if SCOPE_DISABLE_##scope \
// _log(sev, file, line, __VA_ARGS__) \
// #endif

#define _RAW_LOG(sev, scope, file, line, ...)                                                                          \
    do {                                                                                                               \
        if (sev <= EXTRACT_LEVEL(scope)) {                                                                             \
            _log(sev, scope, file, line, __VA_ARGS__);                                                                 \
        }                                                                                                              \
    } while (0)

#if defined(LOG_LEVEL_VERBOSE)
#define VERBOSE_LOG(SCOPE, ...) _RAW_LOG(LEVEL_VERBOSE, SCOPE, __FILE__, __LINE__, __VA_ARGS__)
#else
#define VERBOSE_LOG(SCOPE, ...)
#endif

#if defined(LOG_LEVEL_INFO)
#define INFO_LOG(SCOPE, ...) _RAW_LOG(LEVEL_INFO, SCOPE, __FILE__, __LINE__, __VA_ARGS__)
#else
#define INFO_LOG(SCOPE, ...)
#endif

#if defined(LOG_LEVEL_DEBUG)
#define DEBUG_LOG(SCOPE, ...) _RAW_LOG(LEVEL_DEBUG, SCOPE, __FILE__, __LINE__, __VA_ARGS__)
#else
#define DEBUG_LOG(SCOPE, ...)
#endif

#if defined(LOG_LEVEL_WARNING)
#define WARNING_LOG(SCOPE, ...) _RAW_LOG(LEVEL_WARNING, SCOPE, __FILE__, __LINE__, __VA_ARGS__)
#else
#define WARNING_LOG(SCOPE, ...)
#endif

#if defined(LOG_LEVEL_ERROR)
#define ERROR_LOG(SCOPE, ...) _RAW_LOG(LEVEL_ERROR, SCOPE, __FILE__, __LINE__, __VA_ARGS__)
#else
#define ERROR_LOG(SCOPE, ...)
#endif

#if defined(LOG_LEVEL_CRITICAL)
#define CRITICAL_LOG(SCOPE, ...) _RAW_LOG(LEVEL_CRITICAL, SCOPE, __FILE__, __LINE__, __VA_ARGS__)
#else
#define CRITICAL_LOG(SCOPE, ...)
#endif

#ifdef linux
#include <time.h>
#undef assert
#include <lib/grim/assert.h>

u64 _clock_nano() {
    struct timespec tp;
    // clockid_t __clock_id, struct timespec *__tp)
    i32             result = clock_gettime(CLOCK_MONOTONIC_RAW, &tp);
    if (unlikely(result == -1)) {
        // TODO errno
        CRITICAL_LOG(SCOPE_PERF_LINUX, "Failed to read CLOCK_MONOTONIC_RAW.");
    }
    u64 total_ns = ((u64)tp.tv_sec * 1000000000ULL) + tp.tv_nsec;
    return total_ns;
}

#if PERF_CYCLES == 1
#define time_start() get_clocks();

#define time_checkpoint(scope, ckpt, start)                                                                            \
    do {                                                                                                               \
        u64 now = get_clocks();                                                                                        \
        u64 dt  = now - start;                                                                                         \
        start   = now;                                                                                                 \
                                                                                                                       \
        DEBUG_LOG(scope, ckpt " took: %lu CYCLES", dt);                                                                \
    } while (0)

#define time_end(scope, start)                                                                                         \
    do {                                                                                                               \
        u64 now = get_clocks();                                                                                        \
        u64 dt  = now - start;                                                                                         \
        DEBUG_LOG(scope, "Took: %lu CYCLES", dt);                                                                      \
    } while (0)

#elif PERF_NANOS == 1
#define time_start() _clock_nano();

#define time_checkpoint(scope, ckpt, start)                                                                            \
    do {                                                                                                               \
        u64 now = _clock_nano();                                                                                       \
        u64 _dt = now - start;                                                                                         \
        start   = now;                                                                                                 \
                                                                                                                       \
        DEBUG_LOG(scope, ckpt " took: %lu ns", _dt);                                                                   \
    } while (0)

#define time_end(scope, start)                                                                                         \
    do {                                                                                                               \
        u64 _now = _clock_nano();                                                                                      \
        u64 _dt  = _now - start;                                                                                       \
        DEBUG_LOG(scope, "Took: %lu ns", _dt);                                                                         \
    } while (0)

#endif

//
// void perf_record_start() {
//     // pid_t              pid = syscall(SYS_getpid);
//     pid_t thread_id = syscall(SYS_gettid);
//     pid_t pid = 0; // 0 means calling process/thread
//     // u32                core_id = -1;
//     // unsigned long long timestamp = __rdtscp(&core_id);
//
//     struct perf_event_attr attr = {0};
//     int                    cpu = -1; // specifies any cpu
//     int                    group_fd = -1;
//     unsigned long          flags = 0;
//     i32                    file_descriptor = syscall(SYS_perf_event_open, &attr, 0, -1, group_fd, flags);
//
//     attr.type = PERF_TYPE_HARDWARE;
//     attr.size = sizeof(struct perf_event_attr);
//     attr.config = PERF_COUNT_HW_CACHE_MISSES;
//     attr.config = PERF_COUNT_HW_BRANCH_MISSES;
//     attr.config = PERF_COUNT_HW_STALLED_CYCLES_FRONTEND;
//     attr.config = PERF_COUNT_HW_STALLED_CYCLES_BACKEND;
//     attr.config = PERF_COUNT_HW_REF_CPU_CYCLES;
//
//     attr.type = PERF_TYPE_SOFTWARE;
//     attr.config = PERF_COUNT_SW_CONTEXT_SWITCHES;
//     attr.config = PERF_COUNT_SW_CPU_MIGRATIONS;
//     attr.config = PERF_COUNT_SW_ALIGNMENT_FAULTS; // unaligned memory accesses
//     attr.config = PERF_COUNT_SW_EMULATION_FAULTS; // unimplemented instruction that needed to be emulated
//
//     attr.type = PERF_TYPE_HW_CACHE;
//     // Data cache misses
//     attr.config =
//         (PERF_COUNT_HW_CACHE_L1D) | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
//     // Instrution cache misses
//     attr.config =
//         (PERF_COUNT_HW_CACHE_L1I) | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
//     // Branch Prediction Unit misses
//     attr.config =
//         (PERF_COUNT_HW_CACHE_BPU) | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
//
//     attr.sample_type = PERF_SAMPLE_READ; // all in group, not just leader
//     attr.read_format = PERF_FORMAT_GROUP;
//     /*
//      * When creating an event group, typically the group leader is
//               initialized with disabled set to 1 and any child events are
//               initialized with disabled set to 0.  Despite disabled being
//               0, the child events will not start until the group leader
//               is enabled.
//      * */
//     attr.disabled = 0;
//
//     attr.branch_sample_type = PERF_SAMPLE_BRANCH_PLM_ALL | PERF_SAMPLE_BRANCH_ANY;
//     attr.use_clockid = 1;
//     attr.clockid = CLOCK_BOOTTIME;
//
//     // Make group_fd the original call, it is the group leader/parent
//     i32 file_descriptor2 = syscall(SYS_perf_event_open, &attr, 0, -1, file_descriptor, flags);
//
//     // ioctl(file_descriptor2)
//     // prctl(file_descriptor2)
//
//     // fclose(file_descriptor) & fcntl(file_descriptor);
// }

#elif defined(win32)
DWORD thread_id = GetCurrentThreadId();
#endif

#include <lib/grim/mem/str.h>

typedef struct _LogSpan {
    Str name;
    u64 log_id;
    rop(struct _LogSpan) parent;
} _LogSpan;

static _Thread_local rwp(_LogSpan) _current_log_span = NULL;
static rwp(Arena) _span_arena; // TODO thread safety

void _span_enter(Str name) {
    // arena_alloc(_span_arena, sizeof());
    _LogSpan* span = arena_alloc_aligned(_span_arena, _LogSpan, 1);

    u32 core_id   = -1;
    u64 timestamp = __rdtscp(&core_id);

    span->name                   = name;
    span->log_id                 = timestamp;
    *(_LogSpan**)(&span->parent) = _current_log_span;

    // enter message
    _current_log_span = span; // TODO thread safety
    DEBUG_LOG(SCOPE_SPAN, "SPAN ENTER: %.*s", str_fmt(name));
}
#define span_enter(name) _span_enter(create_string(name))

void _span_exit() {
    _LogSpan current  = *_current_log_span;
    _current_log_span = current.parent;

    // exit message
    DEBUG_LOG(SCOPE_SPAN, "SPAN EXIT: %.*s", str_fmt(current.name));
}
#define span_exit() span_exit

#endif
