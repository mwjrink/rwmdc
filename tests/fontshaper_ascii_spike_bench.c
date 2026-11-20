/* Run tests/fontshaper_ascii_spike_bench.py from the repository root.
 * Reuse the existing font/shaper setup; never run its Unicode tests on the spike.
 * Final glyph, run and per-codepoint break metadata are compared outside timing.
 */
#define main state_regression_main
#include "fontshaper_context_test.c"
#undef main
#include <time.h>
#include <stddef.h>

#ifndef BENCH_CAPACITY
#define BENCH_CAPACITY 2048
#endif
static volatile unsigned long sink;
static int feature_overrides;
static struct {
    int id, codepoint, source, user_id, advance_x, advance_y, offset_x, offset_y;
    float x, y;
} flat[BENCH_CAPACITY];

#if defined(FONTSHAPER_BENCH_MANAGED) && FONTSHAPER_STATE_API_VERSION < 4
// Only for benchmarking historical allocator-based headers, not production.
static void* bench_allocate(void* data, FontShaperUn size, FontShaperUn alignment) {
    (void)data;
    CHECK(alignment <= _Alignof(max_align_t));
    return malloc((size_t)size);
}

static void bench_free(void* data, void* memory, FontShaperUn size, FontShaperUn alignment) {
    (void)data; (void)size; (void)alignment;
    free(memory);
}
#endif

static double seconds(void) {
    struct timespec time;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &time) == 0);
    return (double)time.tv_sec + (double)time.tv_nsec * 1e-9;
}

__attribute__((noinline)) static unsigned paragraph(TestState* state, FontShaperFont* font, const char* text,
                          int length, float scale, float line_height) {
    FontShaperState* shaper = state->shaper;
#ifdef FONTSHAPER_BENCH_BATCH
    FontShaperFeatureOverride features[] = {
        {FONTSHAPER_FEATURE_TAG_liga, 0}, {FONTSHAPER_FEATURE_TAG_kern, 1},
    };
    FontShaperShapeOptions options = {.direction = FONTSHAPER_DIRECTION_LTR};
    if (feature_overrides) { options.features = features; options.feature_count = 2; }
#if FONTSHAPER_STATE_API_VERSION >= 6
    FontShaperU32 shaped_count = font_shaper_shape(shaper, text, (FontShaperU32)length, options);
    CHECK(shaped_count == font_shaper_shape_glyph_count(shaper));
    CHECK(font_shaper_shape_error(shaper) == 0);
#else
    CHECK(font_shaper_shape(shaper, text, (FontShaperU32)length, options) == 0);
#endif
#else
#ifdef FONTSHAPER_BENCH_MANAGED
    CHECK(font_shaper_shape_begin(shaper, FONTSHAPER_DIRECTION_LTR, FONTSHAPER_LANGUAGE_DONT_KNOW) == 0);
    if (feature_overrides) {
        font_shaper_shape_push_feature(shaper, FONTSHAPER_FEATURE_TAG_liga, 0);
        font_shaper_shape_push_feature(shaper, FONTSHAPER_FEATURE_TAG_kern, 1);
    }
#else
    font_shaper_shape_begin_with_memory(shaper, FONTSHAPER_DIRECTION_LTR, FONTSHAPER_LANGUAGE_DONT_KNOW,
                                   state->destination, state->scratch[0], BENCH_CAPACITY, BENCH_CAPACITY);
#endif
    font_shaper_shape_utf8(shaper, text, length, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    font_shaper_shape_end(shaper);
#endif
    unsigned count = 0;
    FontShaperRun run;
    while (font_shaper_shape_run(shaper, &run)) {
        CHECK(run.font == font && run.direction == FONTSHAPER_DIRECTION_LTR);
        FontShaperGlyph* glyph;
        while (font_shaper_glyph_iterator_next(&run.glyphs, &glyph)) {
            CHECK(count < BENCH_CAPACITY);
            int source_id;
#ifdef FONTSHAPER_STATE_API_VERSION
            source_id = (int)glyph->source_index;
#else
            FontShaperShapeCodepoint input;
            CHECK(font_shaper_shape_get_shape_codepoint(shaper, glyph->user_id_or_codepoint_index, &input));
            source_id = input.user_id;
#endif
            flat[count].id = glyph->id;
            flat[count].codepoint = (int)glyph->codepoint;
            flat[count].source = glyph->user_id_or_codepoint_index;
            flat[count].user_id = source_id;
            flat[count].advance_x = glyph->advance_x;
            flat[count].advance_y = glyph->advance_y;
            flat[count].offset_x = glyph->offset_x;
            flat[count].offset_y = glyph->offset_y;
            ++count;
        }
    }
    CHECK(font_shaper_shape_error(shaper) == FONTSHAPER_SHAPE_ERROR_NONE);
    /* ponytail: English space-delimited wrapping at 640 px, not Unicode layout. */
    float x = 0, y = 0;
    unsigned i = 0;
    while (i < count) {
        unsigned end = i + 1;
        float word_width = (float)flat[i].advance_x * scale;
        if (flat[i].codepoint != ' ') {
            while (end < count && flat[end].codepoint != ' ') {
                word_width += (float)flat[end].advance_x * scale;
                ++end;
            }
            if (x > 0 && x + word_width > 640) { x = 0; y += line_height; }
        }
        for (; i < end; ++i) {
            float advance = (float)flat[i].advance_x * scale;
            if (x > 0 && x + advance > 640) { x = 0; y += line_height; }
            flat[i].x = x + (float)flat[i].offset_x * scale;
            flat[i].y = y + (float)flat[i].offset_y * scale;
            x += advance;
        }
    }
    return count;
}

int main(int argc, char** argv) {
    CHECK(argc == 4);
    feature_overrides = atoi(argv[3]);
    FILE* input = fopen(argv[2], "rb");
    CHECK(input);
    char text[BENCH_CAPACITY + 1];
    size_t length = fread(text, 1, sizeof(text), input);
    CHECK(!ferror(input) && feof(input) && length > 0 && length < BENCH_CAPACITY);
    fclose(input);
    text[length] = 0;
    double start = seconds();
    TestFont font = load_font(argv[1]);
    double font_load_us = (seconds() - start) * 1e6;
    start = seconds();
    FontShaperUn backing_bytes = 0;
#ifdef FONTSHAPER_BENCH_MANAGED
    TestState state = {0};
    FontShaperFont* pointers[] = {&font.font};
    FontShaperStateInfo create_info = {pointers, 1, BENCH_CAPACITY, BENCH_CAPACITY, CACHE_BYTES};
#if FONTSHAPER_STATE_API_VERSION >= 4
    backing_bytes = font_shaper_size_of_state(&create_info);
    state.memory = allocate(backing_bytes);
#if FONTSHAPER_STATE_API_VERSION >= 7
    FontShaperState shape_state = font_shaper_create_state(state.memory, backing_bytes, &create_info);
    state.shaper = &shape_state;
    CHECK(font_shaper_shape_error(state.shaper) == 0);
#else
    state.shaper = font_shaper_create_state(state.memory, backing_bytes, &create_info);
    CHECK(state.shaper);
#endif
    state.scratch_size = state.shaper->state_scratch_size;
#else
    fontshaper_allocator allocator = {NULL, bench_allocate, bench_free};
    CHECK(fontshaper_CreateShapeContext(&state.shaper, &create_info, allocator) == 0);
    state.scratch_size = state.shaper->OwnedScratchSize;
    backing_bytes = state.shaper->allocation_size;
#endif
#else
    TestState state = make_test_state(&font, 1, CACHE_BYTES);
    state.input_capacity = state.glyph_capacity = BENCH_CAPACITY;
    free(state.destination);
    state.destination = allocate(font_shaper_size_of_glyph_storage(BENCH_CAPACITY));
    resize_scratch(&state, FONTSHAPER_LANGUAGE_DONT_KNOW);
    if (feature_overrides) {
        font_shaper_shape_push_feature(state.shaper, FONTSHAPER_FEATURE_TAG_liga, 0);
        font_shaper_shape_push_feature(state.shaper, FONTSHAPER_FEATURE_TAG_kern, 1);
    }
#endif
    FontShaperFontInfo21 info = {0};
    info.base.size = sizeof(info);
    font_shaper_get_font_info2(&font.font, &info.base);
    CHECK(info.units_per_em);
    float scale = 18.0f / (float)info.units_per_em;
    float line_height = (float)(info.ascent - info.descent + info.line_gap) * scale;
    double setup_us = (seconds() - start) * 1e6;
    start = seconds();
    unsigned count = paragraph(&state, &font.font, text, (int)length, scale, line_height);
    double first_us = (seconds() - start) * 1e6;
    for (unsigned i = 0; i < 32; ++i) count = paragraph(&state, &font.font, text, (int)length, scale, line_height);
    FontShaperUn cache_used = state.shaper->cache_memory.used;
    start = seconds();
    for (unsigned i = 0; i < 128; ++i) sink += paragraph(&state, &font.font, text, (int)length, scale, line_height);
    double elapsed = seconds() - start;
    unsigned iterations = (unsigned)(0.04 / elapsed * 128);
    if (iterations < 32) iterations = 32;
    if (iterations > 100000) iterations = 100000;
    printf("bytes=%zu glyphs=%u scratch=%llu cache_used=%llu glyph_storage=%llu state=%d backing=%llu font_load_us=%.3f setup_us=%.3f first_us=%.3f times_us=",
           length, count, (unsigned long long)state.scratch_size, (unsigned long long)cache_used,
           (unsigned long long)font_shaper_size_of_glyph_storage(BENCH_CAPACITY), font_shaper_size_of_state_header(),
           (unsigned long long)backing_bytes, font_load_us, setup_us, first_us);
    for (unsigned sample = 0; sample < 5; ++sample) {
        start = seconds();
        for (unsigned i = 0; i < iterations; ++i) sink += paragraph(&state, &font.font, text, (int)length, scale, line_height);
        printf("%.6f,", (seconds() - start) * 1e6 / (double)iterations);
        CHECK(state.shaper->cache_memory.used == cache_used);
    }
    putchar('\n');
    count = paragraph(&state, &font.font, text, (int)length, scale, line_height);
    for (unsigned i = 0; i < count; ++i)
        printf("%d %d %d %d %d %d %d %d %.6f %.6f\n", flat[i].id, flat[i].codepoint,
               flat[i].source, flat[i].user_id, flat[i].advance_x, flat[i].advance_y,
               flat[i].offset_x, flat[i].offset_y, flat[i].x, flat[i].y);
    for (FontShaperU32 i = 0; i < state.shaper->run_count; ++i) {
        FontShaperInternalRunMetadata* run = &state.shaper->runs[i];
        CHECK(run->config && run->config->font == &font.font);
        printf("R %u %u %u %u %u %u\n", run->config->script, run->direction,
               run->paragraph_direction, run->flags, state.shaper->run_boundaries[i],
               state.shaper->run_boundaries[i + 1]);
    }
    for (FontShaperUn i = 0; i <= state.shaper->input_codepoint_count; ++i) {
        FontShaperShapeCodepoint* input = font_shaper_internal_input_codepoint(state.shaper, i, 0);
        CHECK(input);
        printf("B %u %u %u %u %u %d\n", input->codepoint, input->break_flags,
               input->script, input->direction, input->paragraph_direction, input->user_id);
    }
#ifdef FONTSHAPER_BENCH_MANAGED
#if FONTSHAPER_STATE_API_VERSION >= 4
    free(state.memory);
#else
    fontshaper_DestroyShapeContext(state.shaper);
#endif
#else
    free_test_state(&state);
#endif
    free(font.compiled);
    free(font.blob);
    return 0;
}
