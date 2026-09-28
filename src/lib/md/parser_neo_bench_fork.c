/* Benchmark-only translation unit. The owned fork is not modified. */
#define RWMD_MARKDOWN_IMPLEMENTATION
#include "parser.h"

static MdParser benchmark_parser;
static uint64_t benchmark_checksum;
static bool benchmark_noop;

static int benchmark_event(const MdEvent* event, void* unused) {
    (void)unused;
    benchmark_checksum += event->kind + event->type + event->source.begin + event->source.end;
    return 0;
}

static int benchmark_noop_event(const MdEvent* event, void* unused) {
    (void)event;
    (void)unused;
    return 0;
}

int fork_bench_parse(const char* source, uint32_t length, uint32_t flags,
                     uint64_t* peak, uint64_t* events) {
    MdParserProfile profile;
    int result = md_parser_parse(&benchmark_parser, source, length, flags, benchmark_noop ? benchmark_noop_event : benchmark_event, NULL, &profile);
    *peak = profile.arena_peak_bytes;
    *events = profile.event_count;
    return result;
}

uint64_t fork_bench_checksum(void) { return benchmark_checksum; }
void fork_bench_reset(void) {
    md_parser_destroy(&benchmark_parser);
    benchmark_noop = getenv("MD_BENCH_FORK_NOOP") != NULL;
}
