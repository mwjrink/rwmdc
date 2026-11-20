/* Built against both headers by fontshaper_ascii_spike_bench.py.
 * Compare glyphs plus complete break/run metadata on fast and fallback paths.
 */
#define main state_regression_main
#include "fontshaper_context_test.c"
#undef main

static int font_index(FontShaperFont* font, TestFont* fonts) {
    if (!font) return -1;
    for (int i = 0; i < 3; ++i) if (font == &fonts[i].font) return i;
    CHECK(0);
    return -1;
}

static void check_end(TestState* state, TestFont* fonts, int fast, unsigned misses) {
    FontShaperState* shaper = state->shaper;
    font_shaper_shape_end(shaper);
    CHECK(font_shaper_shape_error(shaper) == FONTSHAPER_SHAPE_ERROR_NONE);
#ifdef SPIKE_TEST
    CHECK((shaper->ascii_state == 2) == fast);
    CHECK(shaper->ascii_miss_count == misses);
#else
    (void)misses;
    (void)fast;
#endif
    FontShaperRun run;
    while (font_shaper_shape_run(shaper, &run)) {
        printf("R %d %u %u %u %u\n", font_index(run.font, fonts),
               run.script, run.direction, run.paragraph_direction, run.flags);
        FontShaperGlyph* glyph;
        while (font_shaper_glyph_iterator_next(&run.glyphs, &glyph)) {
            FontShaperShapeCodepoint input;
            CHECK(font_shaper_shape_get_shape_codepoint(shaper, glyph->user_id_or_codepoint_index, &input));
            printf("G %d %d %u %d %d %d %d %d %d %u %u %u\n", font_index(run.font, fonts),
                   glyph->id, glyph->codepoint, glyph->user_id_or_codepoint_index, input.user_id,
                   glyph->advance_x, glyph->advance_y, glyph->offset_x, glyph->offset_y,
                   run.direction, run.paragraph_direction, run.script);
        }
    }
    CHECK(font_shaper_shape_error(shaper) == FONTSHAPER_SHAPE_ERROR_NONE);
    for (FontShaperUn i = 0; i <= shaper->input_codepoint_count; ++i) {
        FontShaperShapeCodepoint* input = font_shaper_internal_input_codepoint(shaper, i, 0);
        CHECK(input);
        printf("B %u %u %u %u %u %d %d %d %u\n", input->codepoint, input->break_flags,
               input->script, input->direction, input->paragraph_direction, input->user_id,
               font_index(input->font, fonts), font_index(input->mapped_font, fonts), input->mapped_glyph_id);
    }
    memset(shaper->pass_memory.data, 0xa5, (size_t)shaper->pass_memory.used);
}

static void add(FontShaperState* shaper, const char* text) {
    font_shaper_shape_utf8(shaper, text, (int)strlen(text), FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
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
            font_shaper_shape_begin_with_memory(state.shaper, FONTSHAPER_DIRECTION_LTR, 0, state.destination,
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
        FontShaperState* shaper = state.shaper;
        printf("scenario %u\n", scenario);
        FontShaperDirection direction = FONTSHAPER_DIRECTION_LTR;
        if (scenario == 5) direction = FONTSHAPER_DIRECTION_RTL;
        if (scenario == 6) direction = FONTSHAPER_DIRECTION_DONT_KNOW;
        resize_scratch(&state, FONTSHAPER_LANGUAGE_TURKISH);
        font_shaper_shape_begin_with_memory(shaper, direction, FONTSHAPER_LANGUAGE_TURKISH, state.destination,
                             state.scratch[0], INPUT_CAPACITY, GLYPH_CAPACITY);
        /* Source IDs are deliberately unrelated to codepoint positions. */
        font_shaper_shape_utf8_with_user_id(shaper, "office ", 7, 100, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
        if (scenario == 0) {
            font_shaper_shape_push_feature(shaper, FONTSHAPER_FEATURE_TAG_liga, 0);
            add(shaper, "office fi");
            CHECK(font_shaper_shape_pop_feature(shaper, FONTSHAPER_FEATURE_TAG_liga));
        } else if (scenario == 1) {
            font_shaper_shape_begin_manual_runs(shaper);
            font_shaper_shape_next_manual_run(shaper, FONTSHAPER_DIRECTION_LTR, FONTSHAPER_SCRIPT_LATIN);
            add(shaper, "office fi");
            font_shaper_shape_end_manual_runs(shaper);
        } else if (scenario == 2) {
            font_shaper_shape_manual_break(shaper);
            add(shaper, "office fi");
        } else if (scenario == 3) {
            CHECK(font_shaper_shape_push_font(shaper, &fonts[0].font));
            CHECK(font_shaper_shape_pop_font(shaper) == &fonts[0].font);
            add(shaper, "office fi");
        } else if (scenario == 4) {
            int points[] = {'A', 'V', 0x301};
            font_shaper_shape_utf32_with_user_id(shaper, points, 3, 200, 2);
        } else if (scenario == 7) {
            font_shaper_shape_utf8_with_user_id(shaper, "\xff fi", 4, 300, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
        } else add(shaper, "office fi");
        unsigned misses = 1;
        if (scenario == 5 || scenario == 6) misses = 0;
        check_end(&state, fonts, 0, misses);
        free_test_state(&state);
    }
    TestState equivalent = make_test_state(fonts, 1, CACHE_BYTES);
    font_shaper_shape_begin_with_memory(equivalent.shaper, FONTSHAPER_DIRECTION_LTR, 0, equivalent.destination,
                         equivalent.scratch[0], INPUT_CAPACITY, GLYPH_CAPACITY);
    font_shaper_shape_push_feature(equivalent.shaper, FONTSHAPER_FEATURE_TAG_liga, 0);
    font_shaper_shape_push_feature(equivalent.shaper, FONTSHAPER_FEATURE_TAG_kern, 1);
    add(equivalent.shaper, "office ");
    CHECK(font_shaper_shape_pop_feature(equivalent.shaper, FONTSHAPER_FEATURE_TAG_kern));
    CHECK(font_shaper_shape_pop_feature(equivalent.shaper, FONTSHAPER_FEATURE_TAG_liga));
    font_shaper_shape_push_feature(equivalent.shaper, FONTSHAPER_FEATURE_TAG_kern, 1);
    font_shaper_shape_push_feature(equivalent.shaper, FONTSHAPER_FEATURE_TAG_liga, 0);
    add(equivalent.shaper, "office fi");
    check_end(&equivalent, fonts, 1, 0);
    free_test_state(&equivalent);
    TestState missing = make_test_state(fonts + 1, 1, CACHE_BYTES);
    for (int point = 32; point <= 126; ++point) {
        if (font_shaper_codepoint_to_glyph_id(&fonts[1].font, point)) continue;
        font_shaper_shape_begin_with_memory(missing.shaper, FONTSHAPER_DIRECTION_LTR, 0, missing.destination,
                             missing.scratch[0], INPUT_CAPACITY, GLYPH_CAPACITY);
        font_shaper_shape_codepoint(missing.shaper, point);
#ifdef SPIKE_TEST
        CHECK(missing.shaper->ascii_state == 0 && missing.shaper->ascii_miss_count == 1);
#endif
        font_shaper_shape_end(missing.shaper);
        printf("missing-glyph error %u\n", font_shaper_shape_error(missing.shaper));
        break;
    }
    free_test_state(&missing);
    TestState multilingual = make_test_state(fonts, 3, CACHE_BYTES);
    font_shaper_shape_begin_with_memory(multilingual.shaper, FONTSHAPER_DIRECTION_LTR, 0, multilingual.destination,
                         multilingual.scratch[0], INPUT_CAPACITY, GLYPH_CAPACITY);
    add(multilingual.shaper, "office العربية देवनागरी office");
    check_end(&multilingual, fonts, 0, 0);
    free_test_state(&multilingual);
    /* Real resource failures must not be mistaken for eligibility misses. */
    TestState limited = make_test_state(fonts, 1, CACHE_BYTES);
    font_shaper_shape_begin_with_memory(limited.shaper, FONTSHAPER_DIRECTION_LTR, 0, limited.destination,
                         limited.scratch[0], 1, GLYPH_CAPACITY);
    add(limited.shaper, "office");
    CHECK(font_shaper_shape_error(limited.shaper) == FONTSHAPER_SHAPE_ERROR_OUT_OF_MEMORY);
#ifdef SPIKE_TEST
    CHECK(limited.shaper->ascii_miss_count == 0);
#endif
    free_test_state(&limited);
    for (unsigned i = 0; i < 3; ++i) { free(fonts[i].compiled); free(fonts[i].blob); }
    return 0;
}
