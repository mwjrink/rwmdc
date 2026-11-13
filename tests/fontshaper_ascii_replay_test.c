/* Built against both headers by fontshaper_ascii_spike_bench.py.
 * Compare glyphs plus complete break/run metadata on fast and fallback paths.
 */
#define main context_regression_main
#include "fontshaper_context_test.c"
#undef main

static int font_index(fontshaper_font* font, TestFont* fonts) {
    if (!font) return -1;
    for (int i = 0; i < 3; ++i) if (font == &fonts[i].font) return i;
    CHECK(0);
    return -1;
}

static void check_end(TestContext* state, TestFont* fonts, int fast, unsigned misses) {
    fontshaper_shape_context* context = state->context;
    fontshaper_ShapeEnd(context);
    CHECK(fontshaper_ShapeError(context) == FONTSHAPER_SHAPE_ERROR_NONE);
#ifdef SPIKE_TEST
    CHECK((context->AsciiState == 2) == fast);
    CHECK(context->AsciiMissCount == misses);
#else
    (void)misses;
    (void)fast;
#endif
    fontshaper_run run;
    while (fontshaper_ShapeRun(context, &run)) {
        printf("R %d %u %u %u %u\n", font_index(run.Font, fonts),
               run.Script, run.Direction, run.ParagraphDirection, run.Flags);
        fontshaper_glyph* glyph;
        while (fontshaper_GlyphIteratorNext(&run.Glyphs, &glyph)) {
            fontshaper_shape_codepoint input;
            CHECK(fontshaper_ShapeGetShapeCodepoint(context, glyph->UserIdOrCodepointIndex, &input));
            printf("G %d %d %u %d %d %d %d %d %d %u %u %u\n", font_index(run.Font, fonts),
                   glyph->Id, glyph->Codepoint, glyph->UserIdOrCodepointIndex, input.UserId,
                   glyph->AdvanceX, glyph->AdvanceY, glyph->OffsetX, glyph->OffsetY,
                   run.Direction, run.ParagraphDirection, run.Script);
        }
    }
    CHECK(fontshaper_ShapeError(context) == FONTSHAPER_SHAPE_ERROR_NONE);
    for (fontshaper_un i = 0; i <= context->InputCodepointCount; ++i) {
        fontshaper_shape_codepoint* input = fontshaper__InputCodepoint(context, i, 0);
        CHECK(input);
        printf("B %u %u %u %u %u %d %d %d %u\n", input->Codepoint, input->BreakFlags,
               input->Script, input->Direction, input->ParagraphDirection, input->UserId,
               font_index(input->Font, fonts), font_index(input->MappedFont, fonts), input->MappedGlyphId);
    }
    memset(context->PassMemory.Data, 0xa5, (size_t)context->PassMemory.Used);
}

static void add(fontshaper_shape_context* context, const char* text) {
    fontshaper_ShapeUtf8(context, text, (int)strlen(text), FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
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
        TestContext state = make_context(fonts, 1, CACHE_BYTES);
        for (unsigned repeat = 0; repeat < 2; ++repeat) {
            printf("case %u repeat %u\n", i, repeat);
            fontshaper_ShapeBegin(state.context, FONTSHAPER_DIRECTION_LTR, 0, state.destination,
                                 state.scratch[repeat], INPUT_CAPACITY, GLYPH_CAPACITY);
            add(state.context, cases[i].text);
            unsigned misses = 1;
            if (cases[i].fast) misses = 0;
            check_end(&state, fonts, cases[i].fast, misses);
        }
        destroy_context(&state);
    }
    for (unsigned scenario = 0; scenario < 8; ++scenario) {
        TestContext state = make_context(fonts, 1, CACHE_BYTES);
        fontshaper_shape_context* context = state.context;
        printf("scenario %u\n", scenario);
        fontshaper_direction direction = FONTSHAPER_DIRECTION_LTR;
        if (scenario == 5) direction = FONTSHAPER_DIRECTION_RTL;
        if (scenario == 6) direction = FONTSHAPER_DIRECTION_DONT_KNOW;
        resize_scratch(&state, FONTSHAPER_LANGUAGE_TURKISH);
        fontshaper_ShapeBegin(context, direction, FONTSHAPER_LANGUAGE_TURKISH, state.destination,
                             state.scratch[0], INPUT_CAPACITY, GLYPH_CAPACITY);
        /* Source IDs are deliberately unrelated to codepoint positions. */
        fontshaper_ShapeUtf8WithUserId(context, "office ", 7, 100, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
        if (scenario == 0) {
            fontshaper_ShapePushFeature(context, FONTSHAPER_FEATURE_TAG_liga, 0);
            add(context, "office fi");
            CHECK(fontshaper_ShapePopFeature(context, FONTSHAPER_FEATURE_TAG_liga));
        } else if (scenario == 1) {
            fontshaper_ShapeBeginManualRuns(context);
            fontshaper_ShapeNextManualRun(context, FONTSHAPER_DIRECTION_LTR, FONTSHAPER_SCRIPT_LATIN);
            add(context, "office fi");
            fontshaper_ShapeEndManualRuns(context);
        } else if (scenario == 2) {
            fontshaper_ShapeManualBreak(context);
            add(context, "office fi");
        } else if (scenario == 3) {
            CHECK(fontshaper_ShapePushFont(context, &fonts[0].font));
            CHECK(fontshaper_ShapePopFont(context) == &fonts[0].font);
            add(context, "office fi");
        } else if (scenario == 4) {
            int points[] = {'A', 'V', 0x301};
            fontshaper_ShapeUtf32WithUserId(context, points, 3, 200, 2);
        } else if (scenario == 7) {
            fontshaper_ShapeUtf8WithUserId(context, "\xff fi", 4, 300, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
        } else add(context, "office fi");
        unsigned misses = 1;
        if (scenario == 5 || scenario == 6) misses = 0;
        check_end(&state, fonts, 0, misses);
        destroy_context(&state);
    }
    TestContext equivalent = make_context(fonts, 1, CACHE_BYTES);
    fontshaper_ShapeBegin(equivalent.context, FONTSHAPER_DIRECTION_LTR, 0, equivalent.destination,
                         equivalent.scratch[0], INPUT_CAPACITY, GLYPH_CAPACITY);
    fontshaper_ShapePushFeature(equivalent.context, FONTSHAPER_FEATURE_TAG_liga, 0);
    fontshaper_ShapePushFeature(equivalent.context, FONTSHAPER_FEATURE_TAG_kern, 1);
    add(equivalent.context, "office ");
    CHECK(fontshaper_ShapePopFeature(equivalent.context, FONTSHAPER_FEATURE_TAG_kern));
    CHECK(fontshaper_ShapePopFeature(equivalent.context, FONTSHAPER_FEATURE_TAG_liga));
    fontshaper_ShapePushFeature(equivalent.context, FONTSHAPER_FEATURE_TAG_kern, 1);
    fontshaper_ShapePushFeature(equivalent.context, FONTSHAPER_FEATURE_TAG_liga, 0);
    add(equivalent.context, "office fi");
    check_end(&equivalent, fonts, 1, 0);
    destroy_context(&equivalent);
    TestContext missing = make_context(fonts + 1, 1, CACHE_BYTES);
    for (int point = 32; point <= 126; ++point) {
        if (fontshaper_CodepointToGlyphId(&fonts[1].font, point)) continue;
        fontshaper_ShapeBegin(missing.context, FONTSHAPER_DIRECTION_LTR, 0, missing.destination,
                             missing.scratch[0], INPUT_CAPACITY, GLYPH_CAPACITY);
        fontshaper_ShapeCodepoint(missing.context, point);
#ifdef SPIKE_TEST
        CHECK(missing.context->AsciiState == 0 && missing.context->AsciiMissCount == 1);
#endif
        fontshaper_ShapeEnd(missing.context);
        printf("missing-glyph error %u\n", fontshaper_ShapeError(missing.context));
        break;
    }
    destroy_context(&missing);
    TestContext multilingual = make_context(fonts, 3, CACHE_BYTES);
    fontshaper_ShapeBegin(multilingual.context, FONTSHAPER_DIRECTION_LTR, 0, multilingual.destination,
                         multilingual.scratch[0], INPUT_CAPACITY, GLYPH_CAPACITY);
    add(multilingual.context, "office العربية देवनागरी office");
    check_end(&multilingual, fonts, 0, 0);
    destroy_context(&multilingual);
    /* Real resource failures must not be mistaken for eligibility misses. */
    TestContext limited = make_context(fonts, 1, CACHE_BYTES);
    fontshaper_ShapeBegin(limited.context, FONTSHAPER_DIRECTION_LTR, 0, limited.destination,
                         limited.scratch[0], 1, GLYPH_CAPACITY);
    add(limited.context, "office");
    CHECK(fontshaper_ShapeError(limited.context) == FONTSHAPER_SHAPE_ERROR_OUT_OF_MEMORY);
#ifdef SPIKE_TEST
    CHECK(limited.context->AsciiMissCount == 0);
#endif
    destroy_context(&limited);
    for (unsigned i = 0; i < 3; ++i) { free(fonts[i].compiled); free(fonts[i].blob); }
    return 0;
}
