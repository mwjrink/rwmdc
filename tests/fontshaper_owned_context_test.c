/* clang -O2 -I src tests/fontshaper_owned_context_test.c -lm -o /tmp/fontshaper-owned
 * /tmp/fontshaper-owned
 * Add -g -fsanitize=address,undefined -fno-omit-frame-pointer for bounds/lifetimes.
 */
#define main state_regression_main
#include "fontshaper_context_test.c"
#undef main
// Status adapter for the existing failure/equivalence cases. The public API
// returns counts; assert that count and stored status agree on every operation.
static FontShaperShapeError shape_status(FontShaperState* state, const char* text,
                                           FontShaperU32 length, FontShaperShapeOptions options) {
    unsigned count = font_shaper_shape(state, text, length, options);
    if (state) CHECK(count == font_shaper_shape_glyph_count(state));
    return font_shaper_shape_error(state);
}

typedef struct Backing {
    FontShaperState state;
    void* base;
    FontShaperUn size;
} Backing;

static FontShaperState* create(TestFont* fonts, unsigned count, unsigned input_capacity,
                                         unsigned glyph_capacity, FontShaperUn cache_size, Backing* backing) {
    FontShaperFont* pointers[3];
    CHECK(count <= 3);
    for (unsigned i = 0; i < count; ++i) pointers[i] = &fonts[i].font;
    FontShaperStateInfo info = {pointers, count, input_capacity, glyph_capacity, cache_size};
    backing->size = font_shaper_size_of_state(&info);
    CHECK(backing->size && backing->size == font_shaper_size_of_state(&info));
    backing->base = allocate(backing->size);
    // Exercise return-by-value and another copy before the first shaping pass.
    FontShaperState value = font_shaper_create_state(backing->base, backing->size, &info);
    backing->state = value;
    memset(&value, 0xa5, sizeof(value));
    FontShaperState* shaper = &backing->state;
    CHECK(font_shaper_shape_error(shaper) == 0);
    CHECK(!font_shaper_shape_run_count(shaper) && !font_shaper_shape_glyph_count(shaper));
    return shaper; // The pointer array and descriptor are not borrowed.
}

static Result read_result(FontShaperState* shaper) {
    Result result = {0};
    FontShaperState before;
    memcpy(&before, shaper, sizeof(before));
    for (unsigned i = 0; i < font_shaper_shape_run_count(shaper); ++i) {
        FontShaperRun run;
        CHECK(font_shaper_shape_get_run(shaper, i, &run));
        unsigned count = run.glyphs.remaining;
        FontShaperGlyph* glyph;
        while (font_shaper_glyph_iterator_next(&run.glyphs, &glyph)) {
            CHECK(result.count < GLYPH_CAPACITY && count);
            --count;
            FontShaperShapeCodepoint input;
            CHECK(font_shaper_shape_get_shape_codepoint(shaper, glyph->user_id_or_codepoint_index, &input));
            CHECK(glyph->source_index == (FontShaperU32)input.user_id);
            result.glyphs[result.count++] = (Glyph){
                .font = run.font, .id = glyph->id, .codepoint = (int)glyph->codepoint,
                .source = glyph->user_id_or_codepoint_index, .user_id = (int)glyph->source_index,
                .advance_x = glyph->advance_x, .advance_y = glyph->advance_y,
                .offset_x = glyph->offset_x, .offset_y = glyph->offset_y,
                .direction = run.direction, .paragraph_direction = run.paragraph_direction,
            };
        }
        CHECK(!count);
    }
    CHECK(result.count == shaper->glyph_storage->live_count); // Expert reader includes separator glyphs.
    unsigned first = result.count;
    for (unsigned i = font_shaper_shape_run_count(shaper); i > 0; --i) {
        FontShaperRun run;
        CHECK(font_shaper_shape_get_run(shaper, i - 1, &run));
        CHECK(run.glyphs.remaining <= first);
        first -= run.glyphs.remaining;
        unsigned at = first;
        FontShaperGlyph* glyph;
        while (font_shaper_glyph_iterator_next(&run.glyphs, &glyph)) {
            CHECK(at < result.count && glyph->id == result.glyphs[at].id);
            CHECK(glyph->source_index == (FontShaperU32)result.glyphs[at].user_id);
            ++at;
        }
    }
    CHECK(first == 0);
    CHECK(!memcmp(&before, shaper, sizeof(before))); // Indexed reading mutates no state.
    return result;
}

static Result shape_owned(FontShaperState* shaper, const char* text, FontShaperLanguage language,
                           const FontShaperFeatureOverride* features, unsigned count) {
    CHECK(font_shaper_shape_begin(shaper, FONTSHAPER_DIRECTION_DONT_KNOW, language) == FONTSHAPER_SHAPE_ERROR_NONE);
    CHECK(!font_shaper_shape_run_count(shaper) && !font_shaper_shape_glyph_count(shaper));
    for (unsigned i = 0; i < count; ++i) font_shaper_shape_push_feature(shaper, features[i].tag, features[i].value);
    font_shaper_shape_utf8(shaper, text, (int)strlen(text), FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    CHECK(font_shaper_shape_end(shaper) == FONTSHAPER_SHAPE_ERROR_NONE);
    CHECK(shaper->next_run == 0); // End finished execution without consuming the read cursor.
    Result result = read_result(shaper);
    Result again = read_result(shaper);
    same_result(&result, &again);
    CHECK(font_shaper_shape_end(shaper) == FONTSHAPER_SHAPE_ERROR_NONE); // Completion is idempotent.
    return result;
}

static void equivalence(TestFont* fonts, unsigned font_count, FontShaperUn cache_size) {
    Backing a = {0};
    FontShaperState* shaper = create(fonts, font_count, INPUT_CAPACITY, GLYPH_CAPACITY, cache_size, &a);
    TestState reference = make_test_state(fonts, font_count, cache_size);
    const char* texts[] = {
        "office fi AV", "Cafe\xcc\x81, na\xc3\xafve and office.", "", "first\tline\r\n\r\nlast\n",
        "هذا نص عربي لاختبار تشكيل الحروف.",
        "यह एक छोटा अनुच्छेद है जिसमें संयुक्ताक्षरों का परीक्षण है।",
        "Latin ثم العربية ثم Latin. देवनागरी and office.", "office \xff next", "бгдпт office",
    };
    FontShaperFeatureOverride features[] = {{FONTSHAPER_FEATURE_TAG_liga, 0}, {FONTSHAPER_FEATURE_TAG_kern, 1}};
    FontShaperLanguage languages[] = {0, FONTSHAPER_LANGUAGE_TURKISH, FONTSHAPER_FOURCC('S','R','B',' ')};
    for (unsigned l = 0; l < sizeof(languages) / sizeof(*languages); ++l) {
        resize_scratch(&reference, languages[l]);
        CHECK(shaper->state_scratch_size >= reference.scratch_size);
        for (unsigned i = 0; i < sizeof(texts) / sizeof(*texts); ++i) {
            Result expected = shape(&reference, texts[i], languages[l], features, 2);
            Result actual = shape_owned(shaper, texts[i], languages[l], features, 2);
            same_result(&expected, &actual);
            expected = shape(&reference, texts[i], languages[l], NULL, 0);
            actual = shape_owned(shaper, texts[i], languages[l], NULL, 0); // No pop cleanup needed.
            same_result(&expected, &actual);
        }
    }
    free_test_state(&reference);
    free(a.base);
}

static void failures(TestFont* fonts) {
    Backing a = {0};
    FontShaperState* shaper = create(fonts, 1, 2, 8, CACHE_BYTES, &a);
    FontShaperRun run;
    font_shaper_shape_codepoint(shaper, 'x');
    CHECK(font_shaper_shape_end(shaper) == FONTSHAPER_SHAPE_ERROR_GAVE_TEXT_BEFORE_CALLING_BEGIN);
    CHECK(font_shaper_shape_begin(shaper, FONTSHAPER_DIRECTION_LTR, 0) == 0);
    font_shaper_shape_utf8(shaper, "office", 6, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    CHECK(font_shaper_shape_end(shaper) == FONTSHAPER_SHAPE_ERROR_INPUT_CAPACITY);
    CHECK(!font_shaper_shape_run_count(shaper) && !font_shaper_shape_get_run(shaper, 0, &run));
    Result result = shape_owned(shaper, "fi", 0, NULL, 0);
    CHECK(result.count);
    CHECK(font_shaper_shape_begin(shaper, FONTSHAPER_DIRECTION_LTR, 0) == 0);
    font_shaper_shape_utf8(shaper, NULL, 1, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    CHECK(font_shaper_shape_end(shaper) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    CHECK(font_shaper_shape_begin(shaper, 999, 0) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    result = shape_owned(shaper, "fi", 0, NULL, 0);
    CHECK(result.count);
    CHECK(font_shaper_shape_begin(shaper, FONTSHAPER_DIRECTION_LTR, 0) == 0);
    font_shaper_shape_codepoint(shaper, 0xd800);
    CHECK(font_shaper_shape_end(shaper) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    CHECK(font_shaper_shape_begin(shaper, FONTSHAPER_DIRECTION_LTR, 0) == 0);
    font_shaper_shape_begin_manual_runs(shaper);
    font_shaper_shape_next_manual_run(shaper, FONTSHAPER_DIRECTION_LTR, (FontShaperScript)-1);
    CHECK(font_shaper_shape_end(shaper) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    result = shape_owned(shaper, "fi", 0, NULL, 0);
    CHECK(result.count);
    font_shaper_shape_manual_break(shaper);
    CHECK(font_shaper_shape_end(shaper) == FONTSHAPER_SHAPE_ERROR_GAVE_TEXT_BEFORE_CALLING_BEGIN);
    free(a.base);

    a = (Backing){0};
    shaper = create(fonts, 1, INPUT_CAPACITY, 8, CACHE_BYTES, &a);
    CHECK(font_shaper_shape_begin(shaper, FONTSHAPER_DIRECTION_LTR, 0) == 0);
    for (unsigned i = 0; i < 16; ++i) {
        font_shaper_shape_utf8(shaper, "fi ", 3, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
        font_shaper_shape_manual_break(shaper);
    }
    CHECK(font_shaper_shape_end(shaper) == FONTSHAPER_SHAPE_ERROR_GLYPH_CAPACITY);
    CHECK(!font_shaper_shape_run_count(shaper) && !font_shaper_shape_glyph_count(shaper));
    CHECK(!font_shaper_shape_run(shaper, &run)); // A later run's failure is already reported.
    result = shape_owned(shaper, "fi", 0, NULL, 0);
    CHECK(result.count);
    FontShaperUn cache_size = shaper->cache_memory.used;
    free(a.base);

    a = (Backing){0};
    shaper = create(fonts, 1, INPUT_CAPACITY, 8, cache_size, &a);
    result = shape_owned(shaper, "fi", 0, NULL, 0);
    CHECK(result.count && shaper->cache_memory.used == cache_size);
    CHECK(font_shaper_shape_begin(shaper, FONTSHAPER_DIRECTION_LTR, 0) == 0);
    font_shaper_shape_push_feature(shaper, FONTSHAPER_FEATURE_TAG_liga, 0);
    font_shaper_shape_utf8(shaper, "fi", 2, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    CHECK(font_shaper_shape_end(shaper) == FONTSHAPER_SHAPE_ERROR_CACHE_CAPACITY);
    CHECK(!font_shaper_shape_run_count(shaper) && !font_shaper_shape_run(shaper, &run));
    result = shape_owned(shaper, "fi", 0, NULL, 0); // Failed cache construction resets before retry.
    CHECK(result.count);
    free(a.base);
}

static void construction(TestFont* fonts) {
    FontShaperFont* pointers[] = {&fonts[0].font};
    FontShaperStateInfo info = {pointers, 1, INPUT_CAPACITY, GLYPH_CAPACITY, CACHE_BYTES};
    CHECK(!font_shaper_size_of_state(NULL));
    FontShaperState failed = font_shaper_create_state(NULL, 0, &info);
    CHECK(font_shaper_shape_error(&failed) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    failed = font_shaper_create_state(NULL, (FontShaperUn)-1, NULL);
    CHECK(font_shaper_shape_error(&failed) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    info.font_count = 0;
    CHECK(!font_shaper_size_of_state(&info));
    info.font_count = FONTSHAPER_STATE_MAX_FONT_COUNT + 1;
    CHECK(!font_shaper_size_of_state(&info));
    info.font_count = 1;
    info.cache_size = (FontShaperUn)-1;
    CHECK(!font_shaper_size_of_state(&info));
    info.cache_size = CACHE_BYTES;
    info.codepoint_capacity = 0;
    CHECK(!font_shaper_size_of_state(&info));
    info.codepoint_capacity = 1u << 27;
    CHECK(!font_shaper_size_of_state(&info));
    info.codepoint_capacity = INPUT_CAPACITY;
    info.glyph_capacity = (FontShaperU32)-1;
    CHECK(!font_shaper_size_of_state(&info));
    info.glyph_capacity = 0;
    CHECK(!font_shaper_size_of_state(&info));
    info.glyph_capacity = GLYPH_CAPACITY;
    FontShaperFont invalid = {0};
    pointers[0] = &invalid;
    CHECK(!font_shaper_size_of_state(&info));
    pointers[0] = &fonts[0].font;
    FontShaperUn size = font_shaper_size_of_state(&info);
    CHECK(size && size == font_shaper_size_of_state(&info));
    unsigned alignment = FONTSHAPER_ALIGNOF(FontShaperState);
    // Exact query-sized storage works for every base alignment. Guard bytes
    // catch partition overflow; creation failures must not touch the block.
    unsigned char* allocation = allocate(size + 2 * alignment);
    for (unsigned offset = 0; offset < alignment; ++offset) {
        memset(allocation, 0xa5, (size_t)size + 2 * alignment);
        unsigned char* memory = allocation + alignment + offset;
        failed = font_shaper_create_state(memory, size - 1, &info);
        CHECK(font_shaper_shape_error(&failed) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
        failed = font_shaper_create_state(memory, size, NULL);
        CHECK(font_shaper_shape_error(&failed) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
        pointers[0] = &invalid;
        failed = font_shaper_create_state(memory, size, &info);
        CHECK(font_shaper_shape_error(&failed) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
        pointers[0] = &fonts[0].font;
        for (FontShaperUn i = 0; i < size + 2 * alignment; ++i) CHECK(allocation[i] == 0xa5);
        struct { FontShaperState state; } owner = {font_shaper_create_state(memory, size, &info)};
        FontShaperState* shaper = &owner.state;
        CHECK(font_shaper_shape_error(shaper) == 0);
        CHECK((uintptr_t)shaper < (uintptr_t)allocation ||
              (uintptr_t)shaper >= (uintptr_t)allocation + size + 2 * alignment);
        CHECK(!font_shaper_set_state_cache(shaper, NULL, 0));
        CHECK(!font_shaper_shape_push_font(shaper, &fonts[0].font));
        CHECK(!font_shaper_shape_pop_font(shaper));
        pointers[0] = NULL; // The caller's font-pointer array was copied.
        CHECK(shape_status(shaper, "office fi", 9, (FontShaperShapeOptions){0}) == 0);
        CHECK(font_shaper_shape_glyph_count(shaper));
        font_shaper_shape_begin_with_memory(shaper, 0, 0, NULL, NULL, 0, 0);
        CHECK(font_shaper_shape_error(shaper) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
        CHECK(shape_status(shaper, "fi", 2, (FontShaperShapeOptions){0}) == 0);
        pointers[0] = &fonts[0].font;
        owner.state = font_shaper_create_state(memory, size, &info); // Reinitialize, no destroy.
        CHECK(shape_status(shaper, "fi", 2, (FontShaperShapeOptions){0}) == 0);
        for (unsigned i = 0; i < alignment + offset; ++i) CHECK(allocation[i] == 0xa5);
        for (FontShaperUn i = alignment + offset + size; i < size + 2 * alignment; ++i)
            CHECK(allocation[i] == 0xa5);
    }
    free(allocation); // Library never owns or releases the backing block.

    TestState placed = make_test_state(fonts, 1, CACHE_BYTES);
    font_shaper_shape_begin_with_memory(placed.shaper, FONTSHAPER_DIRECTION_LTR, 0, placed.destination,
                                   placed.scratch[0], INPUT_CAPACITY, GLYPH_CAPACITY);
    CHECK(!font_shaper_set_state_cache(placed.shaper, placed.cache, CACHE_BYTES));
    font_shaper_shape_utf8(placed.shaper, "fi", 2, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    CHECK(font_shaper_shape_end(placed.shaper) == 0 && font_shaper_shape_run_count(placed.shaper));
    CHECK(font_shaper_set_state_cache(placed.shaper, placed.cache, CACHE_BYTES));
    FontShaperRun run;
    CHECK(!font_shaper_shape_run_count(placed.shaper) && !font_shaper_shape_get_run(placed.shaper, 0, &run));
    free_test_state(&placed);
}

static void check_flat(FontShaperState* state, const Result* expected) {
    FontShaperUn scratch_used = state->pass_memory.used, cache_used = state->cache_memory.used;
    unsigned i = 0, count = 0, runs = 0;
    unsigned char seen[INPUT_CAPACITY] = {0};
    FontShaperRun run = {0};
    const FontShaperRecord* record;
    const FontShaperRecord* first = NULL;
    FontShaperRecord saved = {0};
    while ((record = font_shaper_next(state))) {
        if (!first) { first = record; saved = *record; }
        if (record->kind == FONTSHAPER_RECORD_HARD_BREAK) {
            CHECK(record == &state->runs[state->read_run].hard_break);
            CHECK(record->hard_break.direction != FONTSHAPER_DIRECTION_DONT_KNOW);
            continue;
        }
        if (record->kind == FONTSHAPER_RECORD_RUN) {
            CHECK(record == &state->runs[state->read_run].output);
            CHECK(font_shaper_shape_get_run(state, state->read_run, &run));
            CHECK(record->run.font_idx < state->font_count);
            CHECK(state->fonts[record->run.font_idx].font == run.font);
            CHECK(record->run.direction == run.direction && record->run.script == run.script);
            if (runs) CHECK(record->run.paragraph_direction == FONTSHAPER_DIRECTION_DONT_KNOW);
            else CHECK(record->run.paragraph_direction != FONTSHAPER_DIRECTION_DONT_KNOW);
            ++runs;
            continue;
        }
        CHECK(record->kind == FONTSHAPER_RECORD_GLYPH && runs);
        while (i < expected->count) {
            unsigned line_class = font_shaper_internal_get_unicode_line_break_class(
                font_shaper_internal_input_codepoint(state, expected->glyphs[i].source, 0)->codepoint);
            if (line_class < FONTSHAPER_LINE_BREAK_CLASS_BK || line_class > FONTSHAPER_LINE_BREAK_CLASS_NL) break;
            ++i;
        }
        CHECK(i < expected->count);
        FontShaperGlyph* canonical;
        for (;;) {
            CHECK(font_shaper_glyph_iterator_next(&run.glyphs, &canonical));
            unsigned line_class = font_shaper_internal_get_unicode_line_break_class(
                font_shaper_internal_input_codepoint(state, canonical->user_id_or_codepoint_index, 0)->codepoint);
            if (line_class < FONTSHAPER_LINE_BREAK_CLASS_BK || line_class > FONTSHAPER_LINE_BREAK_CLASS_NL) break;
        }
        const Glyph* old = &expected->glyphs[i++];
        CHECK(canonical->user_id_or_codepoint_index == old->source);
        CHECK(record == &canonical->output); // Actual retained storage, no iterator copy.
        CHECK(&record->glyph.glyph_id == &canonical->id && &record->glyph.source_idx == &canonical->source_index);
        CHECK(record->glyph.glyph_id == (unsigned)old->id && record->glyph.source_idx == (FontShaperU32)old->user_id);
        CHECK(record->glyph.advance_x == old->advance_x && record->glyph.advance_y == old->advance_y);
        CHECK(record->glyph.offset_x == old->offset_x && record->glyph.offset_y == old->offset_y);
        unsigned cluster = (unsigned)old->source;
        while (cluster && !(font_shaper_internal_input_codepoint(state, cluster, 0)->break_flags & FONTSHAPER_BREAK_FLAG_GRAPHEME))
            --cluster;
        CHECK(cluster < INPUT_CAPACITY);
        FontShaperBreakFlags boundary = 0;
        if (!seen[cluster]) boundary = font_shaper_internal_input_codepoint(state, cluster, 0)->break_flags;
        if (seen[cluster] || (canonical->flags & FONTSHAPER_GLYPH_FLAG_NO_BREAK))
            boundary |= FONTSHAPER_BREAK_FLAG_NO_BREAK;
        if (boundary & FONTSHAPER_BREAK_FLAG_LINE_HARD) boundary &= ~FONTSHAPER_BREAK_FLAG_LINE_SOFT;
        boundary &= FONTSHAPER_BREAK_FLAG_GRAPHEME | FONTSHAPER_BREAK_FLAG_WORD |
                    FONTSHAPER_BREAK_FLAG_LINE_SOFT | FONTSHAPER_BREAK_FLAG_NO_BREAK;
        CHECK(record->glyph.break_flags == boundary);
        seen[cluster] = 1;
        ++count;
    }
    while (i < expected->count) {
        unsigned line_class = font_shaper_internal_get_unicode_line_break_class(
            font_shaper_internal_input_codepoint(state, expected->glyphs[i++].source, 0)->codepoint);
        CHECK(line_class >= FONTSHAPER_LINE_BREAK_CLASS_BK && line_class <= FONTSHAPER_LINE_BREAK_CLASS_NL);
    }
    CHECK(count == font_shaper_shape_glyph_count(state) && !font_shaper_next(state));
    if (first) CHECK(!memcmp(first, &saved, sizeof(saved)));
    CHECK(state->pass_memory.used == scratch_used && state->cache_memory.used == cache_used);
    CHECK(font_shaper_shape_error(state) == 0);
}

static void same_metadata(FontShaperState* a, FontShaperState* b) {
    CHECK(a->input_codepoint_count == b->input_codepoint_count);
    CHECK(font_shaper_shape_run_count(a) == font_shaper_shape_run_count(b));
    for (unsigned i = 0; i <= a->input_codepoint_count; ++i) {
        FontShaperShapeCodepoint* x = font_shaper_internal_input_codepoint(a, i, 0);
        FontShaperShapeCodepoint* y = font_shaper_internal_input_codepoint(b, i, 0);
        CHECK(x->codepoint == y->codepoint && x->user_id == y->user_id);
        CHECK(x->break_flags == y->break_flags && x->script == y->script);
        CHECK(x->direction == y->direction && x->paragraph_direction == y->paragraph_direction);
        CHECK(x->font == y->font && x->mapped_font == y->mapped_font && x->mapped_glyph_id == y->mapped_glyph_id);
        CHECK(x->feature_override_count == y->feature_override_count);
        for (unsigned f = 0; f < x->feature_override_count; ++f) {
            CHECK(x->feature_overrides[f].tag == y->feature_overrides[f].tag);
            CHECK(x->feature_overrides[f].value == y->feature_overrides[f].value);
        }
    }
    for (unsigned i = 0; i < font_shaper_shape_run_count(a); ++i) {
        FontShaperRun x, y;
        CHECK(font_shaper_shape_get_run(a, i, &x) && font_shaper_shape_get_run(b, i, &y));
        CHECK(x.font == y.font && x.script == y.script && x.direction == y.direction);
        CHECK(x.paragraph_direction == y.paragraph_direction && x.flags == y.flags);
        CHECK(a->run_boundaries[i] == b->run_boundaries[i] && a->run_boundaries[i + 1] == b->run_boundaries[i + 1]);
        CHECK(x.glyphs.remaining == y.glyphs.remaining);
    }
}

static void batch_equivalence(TestFont* fonts, FontShaperUn cache_size) {
    Backing a = {0}, b = {0};
    FontShaperState* batch = create(fonts, 3, INPUT_CAPACITY, GLYPH_CAPACITY, cache_size, &a);
    FontShaperState* incremental = create(fonts, 3, INPUT_CAPACITY, GLYPH_CAPACITY, cache_size, &b);
    const char* texts[] = {
        "", "office fi AV", "Cafe\xcc\x81 and office.", "first\tline\r\n\r\nlast\n",
        "هذا نص عربي لاختبار تشكيل الحروف.",
        "यह एक छोटा अनुच्छेद है जिसमें संयुक्ताक्षरों का परीक्षण है।",
        "Latin ثم العربية ثم Latin. देवनागरी and office.",
        "\xcd\x84", "\xe0\xa4\x95\xe0\xa4\xbf", // Decomposition and pre-base mark reordering.
        "\xff" "a\x80" "b\xe2\x82" "c\xc2", "f\0i",
    };
    FontShaperFeatureOverride off[] = {{FONTSHAPER_FEATURE_TAG_liga, 0}, {FONTSHAPER_FEATURE_TAG_kern, 1}};
    FontShaperFeatureOverride duplicates[] = {
        {FONTSHAPER_FEATURE_TAG_liga, 1}, {FONTSHAPER_FEATURE_TAG_kern, 1}, {FONTSHAPER_FEATURE_TAG_liga, 0},
        {FONTSHAPER_FEATURE_TAG_salt, 2},
    };
    for (unsigned lang = 0; lang < 2; ++lang) {
        for (unsigned direction = 0; direction < 3; ++direction) {
            for (unsigned style = 0; style < 3; ++style) {
                FontShaperShapeOptions options = {.direction = direction};
                if (lang) options.language = FONTSHAPER_LANGUAGE_TURKISH;
                if (style == 1) { options.features = off; options.feature_count = 2; }
                if (style == 2) { options.features = duplicates; options.feature_count = 4; }
                for (unsigned t = 0; t < sizeof(texts) / sizeof(*texts); ++t) {
                    unsigned length = (unsigned)strlen(texts[t]);
                    if (t == 8) length = 3; // Embedded NUL, not a string terminator.
                    CHECK(shape_status(batch, texts[t], length, options) == 0);
                    CHECK(font_shaper_shape_begin(incremental, options.direction, options.language) == 0);
                    for (unsigned f = 0; f < options.feature_count; ++f)
                        font_shaper_shape_push_feature(incremental, options.features[f].tag, options.features[f].value);
                    // Unlike plain legacy ShapeUtf8, this advanced append accounts
                    // for skipped malformed bytes, making the mapping oracle explicit.
                    font_shaper_shape_utf8_with_user_id(incremental, texts[t], (int)length, 0,
                                                  FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
                    CHECK(font_shaper_shape_end(incremental) == 0);
                    Result expected = read_result(incremental), actual = read_result(batch);
                    same_result(&expected, &actual);
                    same_metadata(batch, incremental);
                    check_flat(batch, &actual);
                    check_flat(incremental, &expected);
                }
            }
        }
    }
    FontShaperShapeOptions options = {.features = off, .feature_count = 2};
    CHECK(shape_status(batch, "office fi", 9, options) == 0);
    FontShaperUn used = batch->cache_memory.used;
    FontShaperGlyphConfig* config = batch->prepared_glyph_config;
    FontShaperFeatureOverride reordered[] = {off[1], off[0]};
    options.features = reordered;
    memset(batch->state_scratch, 0xa5, (size_t)batch->state_scratch_size);
    CHECK(shape_status(batch, "office fi", 9, options) == 0);
    CHECK(batch->cache_memory.used == used);
    if (cache_size) CHECK(batch->prepared_glyph_config == config);
    reordered[1].value = 1;
    CHECK(shape_status(batch, "office fi", 9, options) == 0);
    CHECK(font_shaper_shape_glyph_count(batch) < 9);
    char text[INPUT_CAPACITY];
    memset(text, 'x', sizeof(text));
    for (unsigned n = 126; n <= INPUT_CAPACITY; ++n) {
        CHECK(shape_status(batch, text, n, (FontShaperShapeOptions){0}) == 0);
        CHECK(batch->input_codepoint_count == n && font_shaper_shape_glyph_count(batch) == n);
        for (unsigned i = 0; i < n; ++i) {
            FontShaperShapeCodepoint input;
            CHECK(font_shaper_shape_get_shape_codepoint(batch, i, &input) && input.user_id == (int)i);
        }
    }
    // Source IDs use actual byte positions; the decoder's skip policy is unchanged.
    CHECK(shape_status(batch, "\x80" "a\x80" "b", 4, (FontShaperShapeOptions){0}) == 0);
    FontShaperShapeCodepoint input;
    CHECK(batch->input_codepoint_count == 2);
    CHECK(font_shaper_shape_get_shape_codepoint(batch, 0, &input) && input.user_id == 1);
    CHECK(font_shaper_shape_get_shape_codepoint(batch, 1, &input) && input.user_id == 3);
    free(a.base);
    free(b.base);
}

static void batch_failures(TestFont* fonts) {
    Backing a = {0};
    FontShaperState* shaper = create(fonts, 1, 2, 8, CACHE_BYTES, &a);
    FontShaperShapeOptions defaults = {0};
    CHECK(shape_status(NULL, "", 0, defaults) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    CHECK(font_shaper_shape(shaper, "fi", 2, defaults) == 1);
    Result flat_expected = read_result(shaper);
    check_flat(shaper, &flat_expected);
    CHECK(shape_status(shaper, NULL, 1, defaults) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    CHECK(!font_shaper_shape_run_count(shaper) && !font_shaper_shape_glyph_count(shaper));
    CHECK(!font_shaper_next(shaper));
    CHECK(shape_status(shaper, NULL, 0, defaults) == 0);
    CHECK(shape_status(shaper, "abc", 3, defaults) == FONTSHAPER_SHAPE_ERROR_INPUT_CAPACITY);
    CHECK(!font_shaper_shape_run_count(shaper));
    // Byte length may exceed input capacity: only decoded records consume it.
    CHECK(shape_status(shaper, "\xc3\xa9\xc3\xa9", 4, defaults) == 0);
    CHECK(shaper->input_codepoint_count == 2);
    CHECK(shape_status(shaper, "\xff\xff\xff", 3, defaults) == 0);
    CHECK(shaper->input_codepoint_count == 0);
    CHECK(shape_status(shaper, "\xc0\x80", 2, defaults) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    CHECK(font_shaper_next(shaper) == NULL);
    CHECK(shape_status(shaper, "\xed\xa0\x80", 3, defaults) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    CHECK(shape_status(shaper, "\xf4\x90\x80\x80", 4, defaults) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    FontShaperShapeOptions invalid = {.direction = 999};
    CHECK(shape_status(shaper, "fi", 2, invalid) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    invalid = (FontShaperShapeOptions){.feature_count = 1};
    CHECK(shape_status(shaper, "fi", 2, invalid) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    FontShaperFeatureOverride features[FONTSHAPER_MAX_SIMULTANEOUS_FEATURES + 1];
    for (unsigned i = 0; i < sizeof(features) / sizeof(*features); ++i)
        features[i] = (FontShaperFeatureOverride){i + 1, 0};
    invalid = (FontShaperShapeOptions){.features = features, .feature_count = sizeof(features) / sizeof(*features)};
    CHECK(shape_status(shaper, "fi", 2, invalid) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    for (unsigned i = 0; i < sizeof(features) / sizeof(*features); ++i)
        features[i] = (FontShaperFeatureOverride){FONTSHAPER_FEATURE_TAG_liga, 0};
    CHECK(shape_status(shaper, "fi", 2, invalid) == 0); // Raw count > limit, only one distinct tag.
    Result off = read_result(shaper);
    FontShaperUn used = shaper->cache_memory.used;
    features[0].value = 1; // Earlier duplicate loses; cache keys do not borrow this array.
    CHECK(shape_status(shaper, "fi", 2, invalid) == 0 && shaper->cache_memory.used == used);
    features[0].tag = FONTSHAPER_FEATURE_TAG_salt;
    CHECK(shape_status(shaper, "fi", 2, defaults) == 0);
    Result on = read_result(shaper);
    CHECK(off.count == 2 && on.count == 1);
    char borrowed[] = {'f', 'i'};
    CHECK(shape_status(shaper, borrowed, 2, defaults) == 0);
    memset(borrowed, 0, sizeof(borrowed));
    Result after = read_result(shaper);
    same_result(&on, &after);
    free(a.base);

    a = (Backing){0};
    shaper = create(fonts, 1, INPUT_CAPACITY, 2, CACHE_BYTES, &a);
    CHECK(shape_status(shaper, "abc", 3, defaults) == FONTSHAPER_SHAPE_ERROR_GLYPH_CAPACITY);
    CHECK(!font_shaper_shape_glyph_count(shaper));
    CHECK(shape_status(shaper, "fi", 2, defaults) == 0);
    FontShaperUn exact = shaper->cache_memory.used;
    free(a.base);
    a = (Backing){0};
    shaper = create(fonts, 1, INPUT_CAPACITY, 8, exact, &a);
    CHECK(shape_status(shaper, "fi", 2, defaults) == 0);
    FontShaperFeatureOverride disable = {FONTSHAPER_FEATURE_TAG_liga, 0};
    FontShaperShapeOptions options = {.features = &disable, .feature_count = 1};
    CHECK(shape_status(shaper, "fi", 2, options) == FONTSHAPER_SHAPE_ERROR_CACHE_CAPACITY);
    CHECK(!font_shaper_shape_run_count(shaper));
    CHECK(shape_status(shaper, "fi", 2, defaults) == 0);
    free(a.base);
}

static void record_stream(TestFont* fonts) {
    Backing backing = {0};
    FontShaperState* state = create(fonts, 3, INPUT_CAPACITY, GLYPH_CAPACITY, CACHE_BYTES, &backing);
    const struct { const char* text; const char* kinds; unsigned glyphs; } cases[] = {
        {"", "", 0}, {"abc", "RGGG", 3},
        {"a\nb", "RGHRG", 2}, {"a\r\nb", "RGHRG", 2},
        {"\n\n", "HH", 0}, {"a\n", "RGH", 1}, {"a\r", "RGH", 1},
        {"a\n\nb", "RGHHRG", 2},
        {"a\xc2\x85" "b", "RGHRG", 2},
        {"a\xe2\x80\xa8" "b", "RGHRG", 2},
        {"a\xe2\x80\xa9" "b", "RGHRG", 2},
        {"a\vb", "RGHRG", 2}, {"a\fb", "RGHRG", 2},
    };
    for (unsigned c = 0; c < sizeof(cases) / sizeof(*cases); ++c) {
        unsigned count = font_shaper_shape(state, cases[c].text, (unsigned)strlen(cases[c].text),
                                          (FontShaperShapeOptions){0});
        CHECK(!font_shaper_shape_error(state) && count == cases[c].glyphs);
        unsigned i = 0, source = 0, glyphs = 0;
        const FontShaperRecord* record;
        while ((record = font_shaper_next(state))) {
            CHECK(i < strlen(cases[c].kinds));
            char kind = 'G';
            if (record->kind == FONTSHAPER_RECORD_RUN) kind = 'R';
            if (record->kind == FONTSHAPER_RECORD_HARD_BREAK) {
                kind = 'H';
                while (cases[c].text[source] && cases[c].text[source] != '\n' &&
                       cases[c].text[source] != '\r' && cases[c].text[source] != '\v' &&
                       cases[c].text[source] != '\f' && (unsigned char)cases[c].text[source] < 128) ++source;
                CHECK(record->hard_break.source_idx == source);
                if (cases[c].text[source] == '\r' && cases[c].text[source + 1] == '\n') ++source;
                ++source;
            }
            if (kind == 'G') {
                CHECK(record->kind == FONTSHAPER_RECORD_GLYPH);
                CHECK(!(record->glyph.break_flags & FONTSHAPER_BREAK_FLAG_LINE_HARD));
                ++glyphs;
            }
            CHECK(kind == cases[c].kinds[i++]);
        }
        CHECK(i == strlen(cases[c].kinds) && glyphs == count);
    }
    // Hard breaks carry the next paragraph's base. LS is only a line break,
    // so it keeps the original base even when the following run is Arabic.
    const char* mixed[] = {"Latin\nالعربية", "Latin\xe2\x80\xa8العربية", "Latin\xe2\x80\xa9العربية"};
    for (unsigned c = 0; c < 3; ++c) {
        CHECK(font_shaper_shape(state, mixed[c], (unsigned)strlen(mixed[c]), (FontShaperShapeOptions){0}));
        unsigned breaks = 0, arabic = 0;
        const FontShaperRecord* record;
        while ((record = font_shaper_next(state))) {
            if (record->kind == FONTSHAPER_RECORD_HARD_BREAK) {
                CHECK(record->hard_break.source_idx == 5);
                FontShaperDirection base = FONTSHAPER_DIRECTION_RTL;
                if (c == 1) base = FONTSHAPER_DIRECTION_LTR;
                CHECK(record->hard_break.direction == base);
                ++breaks;
            }
            if (record->kind == FONTSHAPER_RECORD_RUN && record->run.font_idx == 1) {
                CHECK(record->run.direction == FONTSHAPER_DIRECTION_RTL);
                ++arabic;
            }
        }
        CHECK(breaks == 1 && arabic);
    }
    free(backing.base);
}

static void move_state(TestFont* fonts) {
    Backing backing = {0};
    FontShaperState* state = create(fonts, 1, INPUT_CAPACITY, GLYPH_CAPACITY, CACHE_BYTES, &backing);
    FontShaperState* old = allocate(sizeof(*old));
    *old = *state;
    FontShaperFeatureOverride feature = {FONTSHAPER_FEATURE_TAG_liga, 0};
    FontShaperShapeOptions options = {.features = &feature, .feature_count = 1};
    const char text[] = "office";
    CHECK(font_shaper_shape(old, text, sizeof(text) - 1, options) == 6);
    FontShaperUn cache_used = old->cache_memory.used;
    FontShaperRun run;
    CHECK(font_shaper_shape_get_run(old, 0, &run));
    FontShaperShapeCodepointIterator input = font_shaper_shape_current_codepoints_iterator(old);
    CHECK(font_shaper_next(old)->kind == FONTSHAPER_RECORD_RUN);
    *state = *old;
    free(old);

    FontShaperGlyph* glyph;
    CHECK(font_shaper_glyph_iterator_next(&run.glyphs, &glyph));
    CHECK(font_shaper_next(state) == &glyph->output);
    FontShaperShapeCodepoint codepoint;
    CHECK(font_shaper_shape_codepoint_iterator_next(&input, &codepoint, NULL));
    CHECK(codepoint.codepoint == 'o');
    CHECK(font_shaper_shape(state, text, sizeof(text) - 1, options) == 6);
    CHECK(state->cache_memory.used == cache_used);

    CHECK(font_shaper_shape_begin(state, 0, 0) == FONTSHAPER_SHAPE_ERROR_NONE);
    font_shaper_shape_codepoint(state, 'f');
    FontShaperState moved = *state;
    memset(state, 0xa5, sizeof(*state));
    font_shaper_shape_codepoint(&moved, 'i');
    CHECK(font_shaper_shape_end(&moved) == FONTSHAPER_SHAPE_ERROR_NONE);
    CHECK(font_shaper_shape_glyph_count(&moved) == 1);
    free(backing.base);
}

int main(void) {
    TestFont fonts[] = {
        load_font("/usr/share/fonts/TTF/NotoSerifNerdFont-Regular.ttf"),
        load_font("/usr/share/fonts/noto/NotoSansArabic-Regular.ttf"),
        load_font("/usr/share/fonts/noto/NotoSansDevanagari-Regular.ttf"),
    };
    construction(fonts);
    equivalence(fonts, 1, CACHE_BYTES);
    equivalence(fonts, 3, CACHE_BYTES);
    equivalence(fonts, 3, 0);
    failures(fonts);
    batch_equivalence(fonts, CACHE_BYTES);
    batch_equivalence(fonts, 0);
    batch_failures(fonts);
    record_stream(fonts);
    move_state(fonts);
    for (unsigned i = 0; i < 3; ++i) { free(fonts[i].compiled); free(fonts[i].blob); }
    puts("fontshaper caller storage: deterministic size, exact/unaligned bounds, eager results, passive rereads, language bounds, whole-buffer/options/byte offsets, failure/retry and lifetime checks passed");
    return 0;
}
