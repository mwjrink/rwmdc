/* Retained-output regression for the eager production driver.
 * clang -O2 -I src tests/fontshaper_retained_ranges_test.c -lm -o /tmp/fontshaper-ranges
 * /tmp/fontshaper-ranges
 */
#define main state_regression_main
#include "fontshaper_context_test.c"
#undef main

typedef struct SavedRange {
    fontshaper_run run;
    unsigned first, count;
} SavedRange;

static void begin(TestState* state, const char* text, unsigned spans) {
    fontshaper_state* shaper = state->shaper;
    fontshaper_ShapeBeginWithMemory(shaper, FONTSHAPER_DIRECTION_DONT_KNOW, 0, state->destination,
                         state->scratch[0], state->input_capacity, state->glyph_capacity);
    for (unsigned i = 0; i < spans; ++i) {
        fontshaper_ShapeUtf8(shaper, text, (int)strlen(text), FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
        if (spans > 1) fontshaper_ShapeManualBreak(shaper);
    }
    fontshaper_ShapeEnd(shaper);
    CHECK(!fontshaper_ShapeError(shaper));
}

static Glyph public_glyph(fontshaper_state* shaper, const fontshaper_run* run,
                          const fontshaper_glyph* glyph) {
    fontshaper_shape_codepoint input;
    CHECK(fontshaper_ShapeGetShapeCodepoint(shaper, glyph->UserIdOrCodepointIndex, &input));
    return (Glyph){
        .font = run->Font, .id = glyph->Id, .codepoint = (int)glyph->Codepoint,
        .source = glyph->UserIdOrCodepointIndex, .user_id = input.UserId,
        .advance_x = glyph->AdvanceX, .advance_y = glyph->AdvanceY,
        .offset_x = glyph->OffsetX, .offset_y = glyph->OffsetY,
        .direction = run->Direction, .paragraph_direction = run->ParagraphDirection,
    };
}

static Result collect(TestState* state) {
    fontshaper_state* shaper = state->shaper;
    SavedRange ranges[INPUT_CAPACITY + 1];
    fontshaper_glyph saved[GLYPH_CAPACITY];
    unsigned slots[GLYPH_CAPACITY], run_count = 0;
    Result result = {0};
    while (shaper->NextRun < shaper->RunCount) {
        CHECK(run_count < INPUT_CAPACITY + 1);
        SavedRange* range = &ranges[run_count++];
        CHECK(fontshaper_ShapeRun(shaper, &range->run));
        range->first = range->run.Glyphs.CurrentIndex;
        range->count = 0;
        fontshaper_glyph* glyph;
        while (fontshaper_GlyphIteratorNext(&range->run.Glyphs, &glyph)) {
            CHECK(result.count < GLYPH_CAPACITY);
            memcpy(&saved[result.count], glyph, sizeof(*glyph));
            slots[result.count] = glyph->Ref - 1;
            result.glyphs[result.count++] = public_glyph(shaper, &range->run, glyph);
            ++range->count;
        }
    }
    CHECK(!fontshaper_ShapeError(shaper));
    CHECK(shaper->GlyphStorage.LiveCount == result.count);
    fontshaper_un pass_used = shaper->PassMemory.Used;
    fontshaper_un cache_used = shaper->CacheMemory.Used;
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        Result reread = {0};
        for (unsigned i = 0; i < run_count; ++i) {
            fontshaper_run run;
            CHECK(fontshaper_ShapeGetRun(shaper, i, &run));
            fontshaper_glyph_iterator it = run.Glyphs;
            CHECK(it.CurrentIndex == ranges[i].first && it.Remaining == ranges[i].count);
            for (unsigned j = 0; j < ranges[i].count; ++j) {
                fontshaper_glyph* glyph;
                CHECK(fontshaper_GlyphIteratorNext(&it, &glyph));
                CHECK(reread.count < result.count);
                CHECK(glyph == &shaper->GlyphStorage.Glyphs[slots[reread.count]]);
                CHECK(!memcmp(glyph, &saved[reread.count], sizeof(*glyph)));
                reread.glyphs[reread.count++] = public_glyph(shaper, &ranges[i].run, glyph);
            }
            fontshaper_glyph* end;
            CHECK(!fontshaper_GlyphIteratorNext(&it, &end));
        }
        same_result(&result, &reread);
        CHECK(shaper->PassMemory.Used == pass_used && shaper->CacheMemory.Used == cache_used);
        CHECK(shaper->NextRun == run_count && !shaper->GlyphStorage.RangeActive);
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
    fontshaper_ShapeBeginWithMemory(retained.shaper, FONTSHAPER_DIRECTION_DONT_KNOW, 0,
                                   retained.destination, retained.scratch[0], INPUT_CAPACITY, 8);
    for (unsigned i = 0; i < 16; ++i) {
        fontshaper_ShapeUtf8(retained.shaper, "fi ", 3, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
        fontshaper_ShapeManualBreak(retained.shaper);
    }
    CHECK(fontshaper_ShapeEnd(retained.shaper) == FONTSHAPER_SHAPE_ERROR_OUT_OF_MEMORY);
    fontshaper_run run;
    CHECK(!fontshaper_ShapeRun(retained.shaper, &run));
    CHECK(retained.shaper->GlyphStorage.LiveCount <= 8);
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
    const struct { unsigned font_count; fontshaper_un cache_bytes; } modes[] = {
        {1, CACHE_BYTES}, {3, CACHE_BYTES}, {3, 0},
    };
    for (unsigned mode = 0; mode < sizeof(modes) / sizeof(*modes); ++mode) {
        TestState reference = make_test_state(fonts, modes[mode].font_count, modes[mode].cache_bytes);
        TestState retained = make_test_state(fonts, modes[mode].font_count, modes[mode].cache_bytes);
        for (unsigned repeat = 0; repeat < 2; ++repeat) {
            fontshaper_ShapePushFeature(reference.shaper, FONTSHAPER_FEATURE_TAG_liga, (int)repeat);
            fontshaper_ShapePushFeature(retained.shaper, FONTSHAPER_FEATURE_TAG_liga, (int)repeat);
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
            CHECK(fontshaper_ShapePopFeature(reference.shaper, FONTSHAPER_FEATURE_TAG_liga));
            CHECK(fontshaper_ShapePopFeature(retained.shaper, FONTSHAPER_FEATURE_TAG_liga));
        }
        free_test_state(&retained);
        free_test_state(&reference);
    }
    capacity_check(fonts);
    for (unsigned i = 0; i < 3; ++i) { free(fonts[i].compiled); free(fonts[i].blob); }
    puts("fontshaper retained ranges: output equivalence, stable slots, bounded rereads, Indic clusters and global capacity passed");
    return 0;
}
