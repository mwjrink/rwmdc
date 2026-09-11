#ifndef KB_PROFILE_H
#define KB_PROFILE_H

/* Optional GCC/Clang, header-local profiler. All measurements are TSC ticks,
 * not core clock cycles. Include and start profiling in the same translation
 * unit as the instrumented KB implementation. Use a separate profile per thread.
 * The including program must expose POSIX clock_gettime (e.g. GNU C mode).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(__GNUC__) && !defined(__clang__)
#error "KB profiling requires GCC/Clang cleanup support"
#endif
#if defined(__i386__) || defined(__x86_64__)
#include <cpuid.h>
#endif

typedef enum KbProfileStage {
    KBP_FONT_LOAD, KBP_CONFIG_BUILD, KBP_SHAPE, KBP_CONTEXT_RUN,
    KBP_INPUT, KBP_OUTPUT, KBP_EXECUTE_OP, KBP_NORMALIZE, KBP_GSUB,
    KBP_GPOS, KBP_BUCKET, KBP_BUCKET_INSERT, KBP_BUCKET_SORT,
    KBP_SUBSTITUTE, KBP_SEQUENCE, KBP_COVERAGE, KBP_SUBTABLE,
    KBP_GLYPH_PUSH, KBP_GLYPH_READ, KBP_ADJUST, KBP_COUNT
} KbProfileStage;

typedef struct KbProfileCounter {
    uint64_t calls, inclusive_ticks, exclusive_ticks;
} KbProfileCounter;

typedef struct KbProfile {
    KbProfileCounter counters[KBP_COUNT];
    uint64_t migrations;
} KbProfile;

typedef struct KbProfileScope {
    struct KbProfileScope *parent;
    KbProfile *profile;
    uint64_t start, children;
    unsigned aux;
    KbProfileStage stage;
    bool invalid;
} KbProfileScope;

static __thread KbProfile *kb_profile_active;
static __thread KbProfileScope *kb_profile_top;
static __thread unsigned kb_profile_level;

static inline void kb_profile_fail(const char *message) {
    fprintf(stderr, "KB profiling: %s\n", message);
    abort();
}

static inline bool kb_profile_supported(void) {
#if defined(__i386__) || defined(__x86_64__)
    unsigned a, b, c, d;
    if (!__get_cpuid(1, &a, &b, &c, &d) ||
        !(d & (1u << 4)) || !(d & (1u << 26)))
        return false; /* TSC and SSE2 (LFENCE). */
    return __get_cpuid(0x80000001u, &a, &b, &c, &d) && (d & (1u << 27));
#else
    return false;
#endif
}

static inline uint64_t kb_profile_stamp(unsigned *aux) {
#if defined(__i386__) || defined(__x86_64__)
    unsigned low, high;
    __asm__ volatile("lfence\n\trdtscp\n\tlfence"
                     : "=a"(low), "=d"(high), "=c"(*aux) : : "memory");
    return ((uint64_t)high << 32) | low;
#else
    (void)aux;
    kb_profile_fail("serialized RDTSCP is unavailable on this architecture");
    return 0;
#endif
}

static inline void kb_profile_reset(KbProfile *profile) {
    if (!profile || profile == kb_profile_active)
        kb_profile_fail("reset requires a non-null, inactive profile");
    memset(profile, 0, sizeof(*profile));
}

/* Start/stop surround complete instrumented calls, not scopes still executing.
 * Results accumulate until reset. Level 1 is coarse; level 2 adds hot scopes.
 */
static inline void kb_profile_start(KbProfile *profile, unsigned level) {
    if (!profile || kb_profile_active || kb_profile_top || level < 1 || level > 2)
        kb_profile_fail("start requires an inactive profile and level 1 or 2");
    if (!kb_profile_supported())
        kb_profile_fail("CPU does not support TSC, SSE2 and RDTSCP");
    kb_profile_level = level;
    kb_profile_active = profile;
}

static inline void kb_profile_stop(void) {
    if (kb_profile_top)
        kb_profile_fail("stop called inside an instrumented scope");
    kb_profile_active = NULL;
    kb_profile_level = 0;
}

static inline const char *kb_profile_name(KbProfileStage stage) {
    static const char *const names[KBP_COUNT] = {
        "font_load", "config_build", "shape", "context_run", "input",
        "output", "execute_op", "normalize", "gsub", "gpos", "bucket",
        "bucket_insert", "bucket_sort", "substitute", "sequence", "coverage",
        "subtable", "glyph_push", "glyph_read", "adjust"
    };
    return (unsigned)stage < KBP_COUNT ? names[stage] : "unknown";
}

static inline bool kb_profile_coarse(KbProfileStage stage) {
    switch (stage) {
    case KBP_FONT_LOAD: case KBP_CONFIG_BUILD: case KBP_SHAPE:
    case KBP_CONTEXT_RUN: case KBP_INPUT: case KBP_OUTPUT:
    case KBP_NORMALIZE: case KBP_GSUB: case KBP_GPOS:
        return true;
    default:
        return false;
    }
}

static inline void kb_profile_scope_begin(KbProfileScope *scope, KbProfileStage stage) {
    scope->profile = NULL;
    if (!kb_profile_active)
        return;
    if ((unsigned)stage >= KBP_COUNT)
        kb_profile_fail("invalid stage");
    if (kb_profile_level == 1 && !kb_profile_coarse(stage))
        return;
    scope->profile = kb_profile_active;
    scope->parent = kb_profile_top;
    scope->stage = stage;
    scope->children = 0;
    scope->invalid = false;
    ++scope->profile->counters[stage].calls;
    kb_profile_top = scope;
    scope->start = kb_profile_stamp(&scope->aux);
}

static inline void kb_profile_scope_end(KbProfileScope *scope) {
    if (!scope->profile)
        return;
    unsigned aux;
    uint64_t end = kb_profile_stamp(&aux);
    if (kb_profile_top != scope)
        kb_profile_fail("scope stack is not properly nested");
    kb_profile_top = scope->parent;
    /* Count observed invalid intervals, not unique OS migrations. Returning to
     * the same CPU between reads is unobservable. A rejected child invalidates
     * ancestors even if their own endpoint AUX values happen to match.
     */
    if (aux != scope->aux || end < scope->start) {
        ++scope->profile->migrations;
        scope->invalid = true;
    }
    uint64_t ticks = end - scope->start;
    if (scope->invalid || ticks < scope->children) {
        if (scope->parent)
            scope->parent->invalid = true;
        return;
    }
    KbProfileCounter *counter = &scope->profile->counters[scope->stage];
    counter->inclusive_ticks += ticks;
    counter->exclusive_ticks += ticks - scope->children;
    if (scope->parent)
        scope->parent->children += ticks;
}

/* A declaration plus begin call: use in a braced lexical scope. The live stack
 * points directly at this caller-owned record, never at a returned temporary.
 * GCC/Clang cleanup handles normal exit, return and outward goto (not longjmp).
 */
#define KB_PROFILE_JOIN_INNER(a, b) a##b
#define KB_PROFILE_JOIN(a, b) KB_PROFILE_JOIN_INNER(a, b)
#define KB_PROFILE_SCOPE_IMPL(id, n) \
    KbProfileScope KB_PROFILE_JOIN(kb_profile_scope_, n) \
        __attribute__((cleanup(kb_profile_scope_end))); \
    kb_profile_scope_begin(&KB_PROFILE_JOIN(kb_profile_scope_, n), (id))
#define KB_PROFILE_SCOPE(id) KB_PROFILE_SCOPE_IMPL(id, __COUNTER__)

static inline void kb_profile_require_measurement(void) {
    if (kb_profile_active)
        kb_profile_fail("calibration and overhead measurement require an inactive profile");
    if (!kb_profile_supported())
        kb_profile_fail("CPU does not support TSC, SSE2 and RDTSCP");
}

static inline uint64_t kb_profile_monotonic_ns(void) {
    struct timespec now;
#ifdef CLOCK_MONOTONIC_RAW
    const clockid_t clock_id = CLOCK_MONOTONIC_RAW;
#else
    const clockid_t clock_id = CLOCK_MONOTONIC;
#endif
    if (clock_gettime(clock_id, &now) != 0)
        kb_profile_fail("monotonic clock read failed");
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

/* Calibrate outside timed regions on a pinned CPU. Assumes a constant-rate TSC;
 * frequency is reference ticks/second, unrelated to current core turbo rate.
 * Retry endpoint migrations, then fail rather than fabricate a conversion.
 */
static inline double kb_profile_tsc_hz(void) {
    kb_profile_require_measurement();
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        unsigned aux_start, aux_end;
        uint64_t ns_start = kb_profile_monotonic_ns();
        uint64_t start = kb_profile_stamp(&aux_start);
        uint64_t ns_end;
        do {
            ns_end = kb_profile_monotonic_ns();
        } while (ns_end - ns_start < UINT64_C(20000000));
        uint64_t end = kb_profile_stamp(&aux_end);
        if (aux_start == aux_end && end > start)
            return (double)(end - start) * 1e9 / (double)(ns_end - ns_start);
    }
    kb_profile_fail("could not calibrate TSC without migration; pin the thread");
    return 0.0;
}

/* Best observed back-to-back serialized read delta, in TSC ticks. This is a
 * read-overhead floor, not a subtraction from counters or full scope overhead.
 */
static inline uint64_t kb_profile_read_overhead(void) {
    kb_profile_require_measurement();
    uint64_t best = UINT64_MAX;
    for (unsigned i = 0; i < 1000; ++i) {
        unsigned aux_start, aux_end;
        uint64_t start = kb_profile_stamp(&aux_start);
        uint64_t end = kb_profile_stamp(&aux_end);
        if (aux_start == aux_end && end >= start && end - start < best)
            best = end - start;
    }
    if (best == UINT64_MAX)
        kb_profile_fail("could not measure RDTSCP overhead without migration");
    return best;
}

#endif /* KB_PROFILE_H */
