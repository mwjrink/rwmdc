#ifndef KB_PROFILE_H
#define KB_PROFILE_H

/* Optional GCC/Clang, header-local profiler. All measurements are TSC ticks,
 * not core clock cycles. Include and start profiling in the same translation
 * unit as the instrumented KB implementation. Use a separate profile per thread.
 * The including program must expose POSIX clock_gettime (e.g. GNU C mode).
 */
#include <stdbool.h>
#include <stddef.h>
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
    KBP_GLYPH_PUSH, KBP_GLYPH_READ, KBP_ADJUST,
    KBP_LOOKUP_DECODE, KBP_RULE_DECODE, KBP_BACKTRACK_GATHER,
    KBP_INPUT_GATHER, KBP_LOOKAHEAD_GATHER, KBP_SEQUENCE_COMPARE,
    KBP_GLYPH_FILTER, KBP_GSUB_SEED, KBP_GPOS_METRICS, KBP_GPOS_FIXUP,
    KBP_QUEUE_RESET, KBP_COUNT
} KbProfileStage;

/* SUBTABLE_VISITS counts GSUB loop candidates and GPOS adjustment candidates,
 * before the subtable inclusion/filter checks. RULE_VISITS counts active compiled
 * candidate lanes evaluated and GSUB ligature candidates, not font-setup reads.
 * All active lanes in a SIMD block count, including lanes after its winner.
 * Gather work counts visited glyphs, including skipped glyphs, not sentinel
 * checks or reuse of cached symbols. Compiled forward windows combine input and
 * lookahead under INPUT_GATHER; work uses the program's maximum input boundary.
 * The anchor counts where gathered, not for the dispatch predicate proved by its
 * symbol bucket. Ligature matching and GSUB record-input rescans also count
 * input traversal; mutation/positioning walks do not.
 * FILTERED_GLYPHS counts lookup-filter rejections plus additional Unicode skip
 * decisions (once per decision, not unique glyphs). A lookup rejection counts
 * even where an existing caller subsequently overrides that result.
 * COMPARE_WORDS counts compiled predicate checks (including ALWAYS_TRUE padding)
 * and legacy ID/class or ligature comparisons; excludes coverage/class searches.
 * Fixed-depth compiled selection remains in SEQUENCE timing, not per-stage clocks.
 * SEQUENCE_COMPARE times remaining array comparisons; ligature comparisons remain
 * in gather timing. RULE_DECODE covers raw rule unpacking during compilation, not
 * warm compiled matching. LOOKUP_DECODE covers raw lookup/extension unpacking.
 * SEQUENCE_MATCHES counts successful contextual lookups, including zero-record
 * blocking contexts. SUBSTITUTION_MATCHES
 * counts successful primitive GSUB matches, including CheckOnly and no-op IDs,
 * not glyph mutations or contextual dispatch. Context format counts record
 * compiled-context dispatches, even if their selected symbol bucket is empty.
 */
typedef enum KbProfileWork {
    KBW_SUBTABLE_VISITS, KBW_RULE_VISITS, KBW_BACKTRACK_GLYPHS,
    KBW_INPUT_GLYPHS, KBW_LOOKAHEAD_GLYPHS, KBW_FILTERED_GLYPHS,
    KBW_COMPARE_WORDS, KBW_SEQUENCE_MATCHES, KBW_SUBSTITUTION_MATCHES,
    KBW_COUNT
} KbProfileWork;

typedef struct KbProfileCounter {
    uint64_t calls, inclusive_ticks, exclusive_ticks;
} KbProfileCounter;

typedef enum KbLookupPart {
    KBL_TOTAL, KBL_SORT, KBL_APPLY, KBL_PART_COUNT
} KbLookupPart;

typedef struct KbLookupProfile {
    KbProfileCounter parts[KBL_PART_COUNT];
    unsigned type;
} KbLookupProfile;

typedef struct KbProfile {
    KbProfileCounter counters[KBP_COUNT];
    uint64_t migrations;
    uint64_t work[KBW_COUNT];
    uint64_t context_formats[6];
    KbLookupProfile *lookups[2];
    size_t lookup_counts[2];
} KbProfile;

typedef struct KbProfileScope {
    struct KbProfileScope *parent;
    KbProfile *profile;
    KbProfileCounter *target;
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

/* Reset discards lookup bindings, but never touches or frees their storage.
 * Rebind after every reset to clear caller-owned lookup counters as well.
 */
static inline void kb_profile_reset(KbProfile *profile) {
    if (!profile || profile == kb_profile_active)
        kb_profile_fail("reset requires a non-null, inactive profile");
    memset(profile, 0, sizeof(*profile));
}

/* GSUB is table 0, GPOS table 1. Bind a font-sized, non-overlapping array per
 * table while this profile is inactive; NULL/0 unbinds a table. Binding clears
 * the new array, not the old one. The caller owns all storage and must keep it
 * alive and unmoved until profiling stops, then unbind/reset before freeing it.
 * Profiles and their bound arrays must not be shared by concurrent threads.
 */
static inline void kb_profile_bind_lookups(KbProfile *profile, unsigned table,
                                          KbLookupProfile *lookups, size_t count) {
    if (!profile || profile == kb_profile_active)
        kb_profile_fail("lookup binding requires a non-null, inactive profile");
    if (table >= 2 || (count && !lookups) || count > SIZE_MAX / sizeof(*lookups))
        kb_profile_fail("invalid lookup binding");
    if (count)
        memset(lookups, 0, count * sizeof(*lookups));
    profile->lookups[table] = lookups;
    profile->lookup_counts[table] = count;
}

/* Start/stop surround complete instrumented calls, not scopes still executing.
 * Results accumulate until reset. Level 1 is coarse; level 2 adds hot scopes;
 * level 3 measures coarse stages and bound lookup batches, without work counts.
 */
static inline void kb_profile_start(KbProfile *profile, unsigned level) {
#if defined(KB_PROFILE_CORE_ONLY) && KB_PROFILE_CORE_ONLY
    if (level != 3)
        kb_profile_fail("core-only profiling requires level 3");
#endif
    if (!profile || kb_profile_active || kb_profile_top || level < 1 || level > 3)
        kb_profile_fail("start requires an inactive profile and level 1, 2 or 3");
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
        "subtable", "glyph_push", "glyph_read", "adjust", "lookup_decode",
        "rule_decode", "backtrack_gather", "input_gather", "lookahead_gather",
        "sequence_compare", "glyph_filter", "gsub_seed", "gpos_metrics",
        "gpos_fixup", "queue_reset"
    };
    return (unsigned)stage < KBP_COUNT ? names[stage] : "unknown";
}

static inline const char *kb_profile_work_name(KbProfileWork work) {
    static const char *const names[KBW_COUNT] = {
        "subtable_visits", "rule_visits", "backtrack_glyphs", "input_glyphs",
        "lookahead_glyphs", "filtered_glyphs", "compare_words",
        "sequence_matches", "substitution_matches"
    };
    return (unsigned)work < KBW_COUNT ? names[work] : "unknown";
}

static inline const char *kb_profile_context_name(unsigned index) {
    static const char *const names[6] = {
        "simple_format1", "simple_format2", "simple_format3",
        "chained_format1", "chained_format2", "chained_format3"
    };
    return index < 6 ? names[index] : "unknown";
}

static inline void kb_profile_add_work(KbProfileWork work, uint64_t amount) {
#if defined(KB_PROFILE_CORE_ONLY) && KB_PROFILE_CORE_ONLY
    (void)work;
    (void)amount;
#else
    if (!kb_profile_active || kb_profile_level == 3)
        return;
    if ((unsigned)work >= KBW_COUNT)
        kb_profile_fail("invalid work counter");
    kb_profile_active->work[work] += amount;
#endif
}

static inline void kb_profile_add_context(unsigned index) {
#if defined(KB_PROFILE_CORE_ONLY) && KB_PROFILE_CORE_ONLY
    (void)index;
#else
    if (!kb_profile_active || kb_profile_level == 3)
        return;
    if (index >= 6)
        kb_profile_fail("invalid context format");
    ++kb_profile_active->context_formats[index];
#endif
}

static inline bool kb_profile_coarse(KbProfileStage stage) {
    switch (stage) {
    case KBP_FONT_LOAD: case KBP_CONFIG_BUILD: case KBP_SHAPE:
    case KBP_CONTEXT_RUN: case KBP_INPUT: case KBP_OUTPUT:
    case KBP_NORMALIZE: case KBP_GSUB: case KBP_GPOS:
    case KBP_GSUB_SEED: case KBP_GPOS_METRICS: case KBP_GPOS_FIXUP:
    case KBP_QUEUE_RESET:
        return true;
    default:
        return false;
    }
}

/* Ordinary and lookup scopes share one stack and one cleanup path. Timestamps
 * start after bookkeeping, as in the original stage-only profiler.
 */
static inline void kb_profile_counter_scope_begin(KbProfileScope *scope,
                                                   KbProfileCounter *target) {
    scope->profile = kb_profile_active;
    scope->parent = kb_profile_top;
    scope->target = target;
    scope->children = 0;
    scope->invalid = false;
    ++target->calls;
    kb_profile_top = scope;
    scope->start = kb_profile_stamp(&scope->aux);
}

/* Force both ends inline in core-only builds: a constant disabled stage must
 * disappear entirely, including its cleanup, before any TLS access.
 */
#if defined(KB_PROFILE_CORE_ONLY) && KB_PROFILE_CORE_ONLY
__attribute__((always_inline))
#endif
static inline void kb_profile_scope_begin(KbProfileScope *scope, KbProfileStage stage) {
    scope->profile = NULL;
#if defined(KB_PROFILE_CORE_ONLY) && KB_PROFILE_CORE_ONLY
    if (!kb_profile_coarse(stage))
        return;
#endif
    if (!kb_profile_active)
        return;
    if ((unsigned)stage >= KBP_COUNT)
        kb_profile_fail("invalid stage");
    if (kb_profile_level != 2 && !kb_profile_coarse(stage))
        return;
    scope->stage = stage;
    kb_profile_counter_scope_begin(scope, &kb_profile_active->counters[stage]);
}

static inline void kb_profile_lookup_scope_begin(KbProfileScope *scope, unsigned table,
                                                 size_t index, unsigned type,
                                                 KbLookupPart part) {
    scope->profile = NULL;
    if (!kb_profile_active || kb_profile_level != 3)
        return;
    if (table >= 2 || (unsigned)part >= KBL_PART_COUNT)
        kb_profile_fail("invalid lookup table or part");
    KbLookupProfile *lookups = kb_profile_active->lookups[table];
    if (!lookups)
        return; /* Setup may run before font-sized storage is bound. */
    if (index >= kb_profile_active->lookup_counts[table])
        kb_profile_fail("lookup index exceeds bound storage");
    KbLookupProfile *lookup = &lookups[index];
    if ((lookup->parts[KBL_TOTAL].calls || lookup->parts[KBL_SORT].calls ||
         lookup->parts[KBL_APPLY].calls) && lookup->type != type)
        kb_profile_fail("lookup type changed; reset and rebind for a different font");
    lookup->type = type;
    scope->stage = KBP_COUNT; /* Lookup scopes use target, not a stage index. */
    kb_profile_counter_scope_begin(scope, &lookup->parts[part]);
}

#if defined(KB_PROFILE_CORE_ONLY) && KB_PROFILE_CORE_ONLY
__attribute__((always_inline))
#endif
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
    KbProfileCounter *counter = scope->target;
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
#define KB_PROFILE_LOOKUP_SCOPE_IMPL(table, index, type, part, n) \
    KbProfileScope KB_PROFILE_JOIN(kb_profile_scope_, n) \
        __attribute__((cleanup(kb_profile_scope_end))); \
    kb_profile_lookup_scope_begin(&KB_PROFILE_JOIN(kb_profile_scope_, n), \
                                  (table), (index), (type), (part))
#define KB_PROFILE_LOOKUP_SCOPE(table, index, type, part) \
    KB_PROFILE_LOOKUP_SCOPE_IMPL(table, index, type, part, __COUNTER__)
#if defined(KB_PROFILE_CORE_ONLY) && KB_PROFILE_CORE_ONLY
#define KB_PROFILE_WORK(id, amount) ((void)0)
#define KB_PROFILE_CONTEXT(index) ((void)0)
#define KB_PROFILE_ONLY(...)
#else
#define KB_PROFILE_WORK(id, amount) kb_profile_add_work((id), (amount))
#define KB_PROFILE_CONTEXT(index) kb_profile_add_context((index))
#define KB_PROFILE_ONLY(...) __VA_ARGS__
#endif

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
