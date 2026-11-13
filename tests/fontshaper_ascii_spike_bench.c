/* Run tests/fontshaper_ascii_spike_bench.py from the repository root.
 * Reuse the existing font/context setup; never run its Unicode tests on the spike.
 * Final glyph, run and per-codepoint break metadata are compared outside timing.
 */
#define main context_regression_main
#include "fontshaper_context_test.c"
#undef main
#include <time.h>

#define BENCH_CAPACITY 2048
static volatile unsigned long sink;
static struct {
    int id, codepoint, source, user_id, advance_x, advance_y, offset_x, offset_y;
    float x, y;
} flat[BENCH_CAPACITY];

static double seconds(void) {
    struct timespec time;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &time) == 0);
    return (double)time.tv_sec + (double)time.tv_nsec * 1e-9;
}

__attribute__((noinline)) static unsigned paragraph(TestContext* state, fontshaper_font* font, const char* text,
                          int length, float scale, float line_height) {
    fontshaper_shape_context* context = state->context;
    fontshaper_ShapeBegin(context, FONTSHAPER_DIRECTION_LTR, FONTSHAPER_LANGUAGE_DONT_KNOW,
                         state->destination, state->scratch[0], BENCH_CAPACITY, BENCH_CAPACITY);
    fontshaper_ShapeUtf8(context, text, length, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    fontshaper_ShapeEnd(context);
    unsigned count = 0;
    fontshaper_run run;
    while (fontshaper_ShapeRun(context, &run)) {
        CHECK(run.Font == font && run.Direction == FONTSHAPER_DIRECTION_LTR);
        fontshaper_glyph* glyph;
        while (fontshaper_GlyphIteratorNext(&run.Glyphs, &glyph)) {
            CHECK(count < BENCH_CAPACITY);
            fontshaper_shape_codepoint input;
            CHECK(fontshaper_ShapeGetShapeCodepoint(context, glyph->UserIdOrCodepointIndex, &input));
            flat[count].id = glyph->Id;
            flat[count].codepoint = (int)glyph->Codepoint;
            flat[count].source = glyph->UserIdOrCodepointIndex;
            flat[count].user_id = input.UserId;
            flat[count].advance_x = glyph->AdvanceX;
            flat[count].advance_y = glyph->AdvanceY;
            flat[count].offset_x = glyph->OffsetX;
            flat[count].offset_y = glyph->OffsetY;
            ++count;
        }
    }
    CHECK(fontshaper_ShapeError(context) == FONTSHAPER_SHAPE_ERROR_NONE);
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
    FILE* input = fopen(argv[2], "rb");
    CHECK(input);
    char text[BENCH_CAPACITY + 1];
    size_t length = fread(text, 1, sizeof(text), input);
    CHECK(!ferror(input) && feof(input) && length > 0 && length < BENCH_CAPACITY);
    fclose(input);
    text[length] = 0;
    TestFont font = load_font(argv[1]);
    TestContext state = make_context(&font, 1, CACHE_BYTES);
    state.input_capacity = state.glyph_capacity = BENCH_CAPACITY;
    free(state.destination);
    state.destination = allocate(fontshaper_SizeOfGlyphStorage(BENCH_CAPACITY));
    resize_scratch(&state, FONTSHAPER_LANGUAGE_DONT_KNOW);
    if (atoi(argv[3])) {
        fontshaper_ShapePushFeature(state.context, FONTSHAPER_FEATURE_TAG_liga, 0);
        fontshaper_ShapePushFeature(state.context, FONTSHAPER_FEATURE_TAG_kern, 1);
    }
    fontshaper_font_info2_1 info = {0};
    info.Base.Size = sizeof(info);
    fontshaper_GetFontInfo2(&font.font, &info.Base);
    CHECK(info.UnitsPerEm);
    float scale = 18.0f / (float)info.UnitsPerEm;
    float line_height = (float)(info.Ascent - info.Descent + info.LineGap) * scale;
    unsigned count = 0;
    for (unsigned i = 0; i < 32; ++i) count = paragraph(&state, &font.font, text, (int)length, scale, line_height);
    fontshaper_un cache_used = state.context->CacheMemory.Used;
    double start = seconds();
    for (unsigned i = 0; i < 128; ++i) sink += paragraph(&state, &font.font, text, (int)length, scale, line_height);
    double elapsed = seconds() - start;
    unsigned iterations = (unsigned)(0.04 / elapsed * 128);
    if (iterations < 32) iterations = 32;
    if (iterations > 100000) iterations = 100000;
    printf("bytes=%zu glyphs=%u scratch=%llu cache_used=%llu times_us=", length, count,
           (unsigned long long)state.scratch_size, (unsigned long long)cache_used);
    for (unsigned sample = 0; sample < 5; ++sample) {
        start = seconds();
        for (unsigned i = 0; i < iterations; ++i) sink += paragraph(&state, &font.font, text, (int)length, scale, line_height);
        printf("%.6f,", (seconds() - start) * 1e6 / (double)iterations);
        CHECK(state.context->CacheMemory.Used == cache_used);
    }
    putchar('\n');
    count = paragraph(&state, &font.font, text, (int)length, scale, line_height);
    for (unsigned i = 0; i < count; ++i)
        printf("%d %d %d %d %d %d %d %d %.6f %.6f\n", flat[i].id, flat[i].codepoint,
               flat[i].source, flat[i].user_id, flat[i].advance_x, flat[i].advance_y,
               flat[i].offset_x, flat[i].offset_y, flat[i].x, flat[i].y);
    for (fontshaper_u32 i = 0; i < state.context->RunCount; ++i) {
        fontshaper__run_metadata* run = &state.context->Runs[i];
        CHECK(run->Config && run->Config->Font == &font.font);
        printf("R %u %u %u %u %u %u\n", run->Config->Script, run->Direction,
               run->ParagraphDirection, run->Flags, state.context->RunBoundaries[i],
               state.context->RunBoundaries[i + 1]);
    }
    for (fontshaper_un i = 0; i <= state.context->InputCodepointCount; ++i) {
        fontshaper_shape_codepoint* input = fontshaper__InputCodepoint(state.context, i, 0);
        CHECK(input);
        printf("B %u %u %u %u %u %d\n", input->Codepoint, input->BreakFlags,
               input->Script, input->Direction, input->ParagraphDirection, input->UserId);
    }
    destroy_context(&state);
    free(font.compiled);
    free(font.blob);
    return 0;
}
