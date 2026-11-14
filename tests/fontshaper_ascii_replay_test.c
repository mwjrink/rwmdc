/* Built against both headers by fontshaper_ascii_spike_bench.py.
 * Compare glyphs plus complete break/run metadata on fast and fallback paths.
 */
#define main state_regression_main
#include "fontshaper_context_test.c"
#undef main

static int font_index(fontshaper_font* font, TestFont* fonts) {
    if (!font) return -1;
    for (int i = 0; i < 3; ++i) if (font == &fonts[i].font) return i;
    CHECK(0);
    return -1;
}

static void check_end(TestState* state, TestFont* fonts, int fast, unsigned misses) {
    fontshaper_state* shaper = state->shaper;
    fontshaper_ShapeEnd(shaper);
    CHECK(fontshaper_ShapeError(shaper) == FONTSHAPER_SHAPE_ERROR_NONE);
#ifdef SPIKE_TEST
    CHECK((shaper->AsciiState == 2) == fast);
    CHECK(shaper->AsciiMissCount == misses);
#else
    (void)misses;
    (void)fast;
#endif
    fontshaper_run run;
    while (fontshaper_ShapeRun(shaper, &run)) {
        printf("R %d %u %u %u %u\n", font_index(run.Font, fonts),
               run.Script, run.Direction, run.ParagraphDirection, run.Flags);
        fontshaper_glyph* glyph;
        while (fontshaper_GlyphIteratorNext(&run.Glyphs, &glyph)) {
            fontshaper_shape_codepoint input;
            CHECK(fontshaper_ShapeGetShapeCodepoint(shaper, glyph->UserIdOrCodepointIndex, &input));
            printf("G %d %d %u %d %d %d %d %d %d %u %u %u\n", font_index(run.Font, fonts),
                   glyph->Id, glyph->Codepoint, glyph->UserIdOrCodepointIndex, input.UserId,
                   glyph->AdvanceX, glyph->AdvanceY, glyph->OffsetX, glyph->OffsetY,
                   run.Direction, run.ParagraphDirection, run.Script);
        }
    }
    CHECK(fontshaper_ShapeError(shaper) == FONTSHAPER_SHAPE_ERROR_NONE);
    for (fontshaper_un i = 0; i <= shaper->InputCodepointCount; ++i) {
        fontshaper_shape_codepoint* input = fontshaper__InputCodepoint(shaper, i, 0);
        CHECK(input);
        printf("B %u %u %u %u %u %d %d %d %u\n", input->Codepoint, input->BreakFlags,
               input->Script, input->Direction, input->ParagraphDirection, input->UserId,
               font_index(input->Font, fonts), font_index(input->MappedFont, fonts), input->MappedGlyphId);
    }
    memset(shaper->PassMemory.Data, 0xa5, (size_t)shaper->PassMemory.Used);
}

static void add(fontshaper_state* shaper, const char* text) {
    fontshaper_ShapeUtf8(shaper, text, (int)strlen(text), FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
}

int main(void) {
    TestFont fonts[] = {
        load_font("/usr/share/fonts/TTF/NotoSerifNerdFont-Regular.ttf"),
        load_font("/usr/share/fonts/noto/NotoSansArabic-Regular.ttf"),
        load_font("/usr/share/fonts/noto/NotoSansDevanagari-Regular.ttf"),
    };
    const struct { const char* text; int fast; } cases[] = {
        {"office AV fi", 1}, {"1234 + (56)", 0}, {"", 0}, {"\xc3\xa9 office", 0},
        {"office Cafe\xcc\x81", 0}, {"office AV then Ελληνικά", 0}, {"office\nnext", 0},
        {"office\tnext", 0}, {"office \xff next", 0}, {"1234 + office fi", 1},
        {"() office fi", 1}, {"office 123 AV", 1},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        TestState state = make_test_state(fonts, 1, CACHE_BYTES);
        for (unsigned repeat = 0; repeat < 2; ++repeat) {
            printf("case %u repeat %u\n", i, repeat);
            fontshaper_ShapeBeginWithMemory(state.shaper, FONTSHAPER_DIRECTION_LTR, 0, state.destination,
                                 state.scratch[repeat], INPUT_CAPACITY, GLYPH_CAPACITY);
            add(state.shaper, cases[i].text);
            unsigned misses = 1;
            if (cases[i].fast) misses = 0;
            check_end(&state, fonts, cases[i].fast, misses);
        }
        free_test_state(&state);
    }
    for (unsigned scenario = 0; scenario < 8; ++scenario) {
        TestState state = make_test_state(fonts, 1, CACHE_BYTES);
        fontshaper_state* shaper = state.shaper;
        printf("scenario %u\n", scenario);
        fontshaper_direction direction = FONTSHAPER_DIRECTION_LTR;
        if (scenario == 5) direction = FONTSHAPER_DIRECTION_RTL;
        if (scenario == 6) direction = FONTSHAPER_DIRECTION_DONT_KNOW;
        resize_scratch(&state, FONTSHAPER_LANGUAGE_TURKISH);
        fontshaper_ShapeBeginWithMemory(shaper, direction, FONTSHAPER_LANGUAGE_TURKISH, state.destination,
                             state.scratch[0], INPUT_CAPACITY, GLYPH_CAPACITY);
        /* Source IDs are deliberately unrelated to codepoint positions. */
        fontshaper_ShapeUtf8WithUserId(shaper, "office ", 7, 100, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
        if (scenario == 0) {
            fontshaper_ShapePushFeature(shaper, FONTSHAPER_FEATURE_TAG_liga, 0);
            add(shaper, "office fi");
            CHECK(fontshaper_ShapePopFeature(shaper, FONTSHAPER_FEATURE_TAG_liga));
        } else if (scenario == 1) {
            fontshaper_ShapeBeginManualRuns(shaper);
            fontshaper_ShapeNextManualRun(shaper, FONTSHAPER_DIRECTION_LTR, FONTSHAPER_SCRIPT_LATIN);
            add(shaper, "office fi");
            fontshaper_ShapeEndManualRuns(shaper);
        } else if (scenario == 2) {
            fontshaper_ShapeManualBreak(shaper);
            add(shaper, "office fi");
        } else if (scenario == 3) {
            CHECK(fontshaper_ShapePushFont(shaper, &fonts[0].font));
            CHECK(fontshaper_ShapePopFont(shaper) == &fonts[0].font);
            add(shaper, "office fi");
        } else if (scenario == 4) {
            int points[] = {'A', 'V', 0x301};
            fontshaper_ShapeUtf32WithUserId(shaper, points, 3, 200, 2);
        } else if (scenario == 7) {
            fontshaper_ShapeUtf8WithUserId(shaper, "\xff fi", 4, 300, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
        } else add(shaper, "office fi");
        unsigned misses = 1;
        if (scenario == 5 || scenario == 6) misses = 0;
        check_end(&state, fonts, 0, misses);
        free_test_state(&state);
    }
    TestState equivalent = make_test_state(fonts, 1, CACHE_BYTES);
    fontshaper_ShapeBeginWithMemory(equivalent.shaper, FONTSHAPER_DIRECTION_LTR, 0, equivalent.destination,
                         equivalent.scratch[0], INPUT_CAPACITY, GLYPH_CAPACITY);
    fontshaper_ShapePushFeature(equivalent.shaper, FONTSHAPER_FEATURE_TAG_liga, 0);
    fontshaper_ShapePushFeature(equivalent.shaper, FONTSHAPER_FEATURE_TAG_kern, 1);
    add(equivalent.shaper, "office ");
    CHECK(fontshaper_ShapePopFeature(equivalent.shaper, FONTSHAPER_FEATURE_TAG_kern));
    CHECK(fontshaper_ShapePopFeature(equivalent.shaper, FONTSHAPER_FEATURE_TAG_liga));
    fontshaper_ShapePushFeature(equivalent.shaper, FONTSHAPER_FEATURE_TAG_kern, 1);
    fontshaper_ShapePushFeature(equivalent.shaper, FONTSHAPER_FEATURE_TAG_liga, 0);
    add(equivalent.shaper, "office fi");
    check_end(&equivalent, fonts, 1, 0);
    free_test_state(&equivalent);
    TestState missing = make_test_state(fonts + 1, 1, CACHE_BYTES);
    for (int point = 32; point <= 126; ++point) {
        if (fontshaper_CodepointToGlyphId(&fonts[1].font, point)) continue;
        fontshaper_ShapeBeginWithMemory(missing.shaper, FONTSHAPER_DIRECTION_LTR, 0, missing.destination,
                             missing.scratch[0], INPUT_CAPACITY, GLYPH_CAPACITY);
        fontshaper_ShapeCodepoint(missing.shaper, point);
#ifdef SPIKE_TEST
        CHECK(missing.shaper->AsciiState == 0 && missing.shaper->AsciiMissCount == 1);
#endif
        fontshaper_ShapeEnd(missing.shaper);
        printf("missing-glyph error %u\n", fontshaper_ShapeError(missing.shaper));
        break;
    }
    free_test_state(&missing);
    TestState multilingual = make_test_state(fonts, 3, CACHE_BYTES);
    fontshaper_ShapeBeginWithMemory(multilingual.shaper, FONTSHAPER_DIRECTION_LTR, 0, multilingual.destination,
                         multilingual.scratch[0], INPUT_CAPACITY, GLYPH_CAPACITY);
    add(multilingual.shaper, "office العربية देवनागरी office");
    check_end(&multilingual, fonts, 0, 0);
    free_test_state(&multilingual);
    /* Real resource failures must not be mistaken for eligibility misses. */
    TestState limited = make_test_state(fonts, 1, CACHE_BYTES);
    fontshaper_ShapeBeginWithMemory(limited.shaper, FONTSHAPER_DIRECTION_LTR, 0, limited.destination,
                         limited.scratch[0], 1, GLYPH_CAPACITY);
    add(limited.shaper, "office");
    CHECK(fontshaper_ShapeError(limited.shaper) == FONTSHAPER_SHAPE_ERROR_OUT_OF_MEMORY);
#ifdef SPIKE_TEST
    CHECK(limited.shaper->AsciiMissCount == 0);
#endif
    free_test_state(&limited);
    for (unsigned i = 0; i < 3; ++i) { free(fonts[i].compiled); free(fonts[i].blob); }
    return 0;
}
