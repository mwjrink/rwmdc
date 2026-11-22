/* Retained-output regression for the eager production driver.
 * clang -O2 -I src tests/fontshaper_retained_ranges_test.c -lm -o /tmp/fontshaper-ranges
 * /tmp/fontshaper-ranges
 */
#define main state_regression_main
#include "fontshaper_context_test.c"
#undef main

typedef struct SavedRange {
    FontShaperRun run;
    unsigned first, count;
} SavedRange;

static void begin(TestState* state, const char* text, unsigned spans) {
    FontShaperState* shaper = state->shaper;
    font_shaper_shape_begin_with_memory(shaper, FONTSHAPER_DIRECTION_DONT_KNOW, 0, state->destination,
                         state->scratch[0], state->input_capacity, state->glyph_capacity);
    for (unsigned i = 0; i < spans; ++i) {
        font_shaper_shape_utf8(shaper, text, (int)strlen(text), FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
        if (spans > 1) font_shaper_shape_manual_break(shaper);
    }
    font_shaper_shape_end(shaper);
    CHECK(!font_shaper_shape_error(shaper));
}

static Glyph public_glyph(FontShaperState* shaper, const FontShaperRun* run,
                          const FontShaperGlyph* glyph) {
    FontShaperShapeCodepoint input;
    CHECK(font_shaper_shape_get_shape_codepoint(shaper, glyph->user_id_or_codepoint_index, &input));
    return (Glyph){
        .font = run->font, .id = glyph->id, .codepoint = (int)glyph->codepoint,
        .source = glyph->user_id_or_codepoint_index, .user_id = input.user_id,
        .advance_x = glyph->advance_x, .advance_y = glyph->advance_y,
        .offset_x = glyph->offset_x, .offset_y = glyph->offset_y,
        .direction = run->direction, .paragraph_direction = run->paragraph_direction,
    };
}

static Result collect(TestState* state) {
    FontShaperState* shaper = state->shaper;
    SavedRange ranges[INPUT_CAPACITY + 1];
    FontShaperGlyph saved[GLYPH_CAPACITY];
    unsigned slots[GLYPH_CAPACITY], run_count = 0;
    Result result = {0};
    while (shaper->next_run < shaper->run_count) {
        CHECK(run_count < INPUT_CAPACITY + 1);
        SavedRange* range = &ranges[run_count++];
        CHECK(font_shaper_shape_run(shaper, &range->run));
        range->first = range->run.glyphs.current_index;
        range->count = 0;
        FontShaperGlyph* glyph;
        while (font_shaper_glyph_iterator_next(&range->run.glyphs, &glyph)) {
            CHECK(result.count < GLYPH_CAPACITY);
            memcpy(&saved[result.count], glyph, sizeof(*glyph));
            slots[result.count] = glyph->ref - 1;
            result.glyphs[result.count++] = public_glyph(shaper, &range->run, glyph);
            ++range->count;
        }
    }
    CHECK(!font_shaper_shape_error(shaper));
    CHECK(shaper->glyph_storage->live_count == result.count);
    FontShaperUn pass_used = shaper->pass_memory.used;
    FontShaperUn cache_used = shaper->cache_memory.used;
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        Result reread = {0};
        for (unsigned i = 0; i < run_count; ++i) {
            FontShaperRun run;
            CHECK(font_shaper_shape_get_run(shaper, i, &run));
            FontShaperGlyphIterator it = run.glyphs;
            CHECK(it.current_index == ranges[i].first && it.remaining == ranges[i].count);
            for (unsigned j = 0; j < ranges[i].count; ++j) {
                FontShaperGlyph* glyph;
                CHECK(font_shaper_glyph_iterator_next(&it, &glyph));
                CHECK(reread.count < result.count);
                CHECK(glyph == &shaper->glyph_storage->glyphs[slots[reread.count]]);
                CHECK(!memcmp(glyph, &saved[reread.count], sizeof(*glyph)));
                reread.glyphs[reread.count++] = public_glyph(shaper, &ranges[i].run, glyph);
            }
            FontShaperGlyph* end;
            CHECK(!font_shaper_glyph_iterator_next(&it, &end));
        }
        same_result(&result, &reread);
        CHECK(shaper->pass_memory.used == pass_used && shaper->cache_memory.used == cache_used);
        CHECK(shaper->next_run == run_count && !shaper->glyph_storage->range_active);
    }
    return result;
}

static void capacity_check(TestFont* fonts) {
    TestState reference = make_test_state(fonts, 1, CACHE_BYTES);
    TestState retained = make_test_state(fonts, 1, CACHE_BYTES);
    reference.glyph_capacity = 128;
    retained.glyph_capacity = 8;
    resize_scratch(&reference, 0);
    resize_scratch(&retained, 0);
    begin(&reference, "fi ", 16);
    Result expected = collect(&reference);
    CHECK(expected.count > 8);
    font_shaper_shape_begin_with_memory(retained.shaper, FONTSHAPER_DIRECTION_DONT_KNOW, 0,
                                   retained.destination, retained.scratch[0], INPUT_CAPACITY, 8);
    for (unsigned i = 0; i < 16; ++i) {
        font_shaper_shape_utf8(retained.shaper, "fi ", 3, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
        font_shaper_shape_manual_break(retained.shaper);
    }
    CHECK(font_shaper_shape_end(retained.shaper) == FONTSHAPER_SHAPE_ERROR_OUT_OF_MEMORY);
    FontShaperRun run;
    CHECK(!font_shaper_shape_run(retained.shaper, &run));
    CHECK(retained.shaper->glyph_storage->live_count <= 8);
    free_test_state(&retained);
    free_test_state(&reference);
}

int main(void) {
    TestFont fonts[] = {
        load_font("/usr/share/fonts/TTF/NotoSerifNerdFont-Regular.ttf"),
        load_font("/usr/share/fonts/noto/NotoSansArabic-Regular.ttf"),
        load_font("/usr/share/fonts/noto/NotoSansDevanagari-Regular.ttf"),
    };
    const char* texts[] = {
        "office fi AV", "office Cafe\xcc\x81", "office\n\nnext\n", "",
        "office العربية office देवनागरी office", "هذا نص عربي لاختبار تشكيل الحروف.",
        "यह एक छोटा अनुच्छेद है जिसमें संयुक्ताक्षरों का परीक्षण है।",
        "office \xff next\tline\r\nlast",
    };
    const struct { unsigned font_count; FontShaperUn cache_bytes; } modes[] = {
        {1, CACHE_BYTES}, {3, CACHE_BYTES}, {3, 0},
    };
    for (unsigned mode = 0; mode < sizeof(modes) / sizeof(*modes); ++mode) {
        TestState reference = make_test_state(fonts, modes[mode].font_count, modes[mode].cache_bytes);
        TestState retained = make_test_state(fonts, modes[mode].font_count, modes[mode].cache_bytes);
        for (unsigned repeat = 0; repeat < 2; ++repeat) {
            font_shaper_shape_push_feature(reference.shaper, FONTSHAPER_FEATURE_TAG_liga, (int)repeat);
            font_shaper_shape_push_feature(retained.shaper, FONTSHAPER_FEATURE_TAG_liga, (int)repeat);
            for (unsigned i = 0; i < sizeof(texts) / sizeof(*texts); ++i) {
                begin(&reference, texts[i], 1);
                Result expected = collect(&reference);
                begin(&retained, texts[i], 1);
                Result actual = collect(&retained);
                same_result(&expected, &actual);
            }
            begin(&reference, "office fi AV ", 12);
            Result expected = collect(&reference);
            begin(&retained, "office fi AV ", 12);
            Result actual = collect(&retained);
            same_result(&expected, &actual);
            CHECK(font_shaper_shape_pop_feature(reference.shaper, FONTSHAPER_FEATURE_TAG_liga));
            CHECK(font_shaper_shape_pop_feature(retained.shaper, FONTSHAPER_FEATURE_TAG_liga));
        }
        free_test_state(&retained);
        free_test_state(&reference);
    }
    capacity_check(fonts);
    for (unsigned i = 0; i < 3; ++i) { free(fonts[i].compiled); free(fonts[i].blob); }
    puts("fontshaper retained ranges: output equivalence, stable slots, bounded rereads, Indic clusters and global capacity passed");
    return 0;
}
