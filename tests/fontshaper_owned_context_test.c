/* clang -O2 -I src tests/fontshaper_owned_context_test.c -lm -o /tmp/fontshaper-owned
 * /tmp/fontshaper-owned
 * Add -g -fsanitize=address,undefined -fno-omit-frame-pointer for bounds/lifetimes.
 */
#define main state_regression_main
#include "fontshaper_context_test.c"
#undef main
// Status adapter for the existing failure/equivalence cases. The public API
// returns counts; assert that count and stored status agree on every operation.
static fontshaper_shape_error shape_status(fontshaper_state* state, const char* text,
                                           fontshaper_u32 length, fontshaper_shape_options options) {
    unsigned count = fontshaper_Shape(state, text, length, options);
    if (state) CHECK(count == fontshaper_ShapeGlyphCount(state));
    return fontshaper_ShapeError(state);
}

typedef struct Backing {
    void* base;
    fontshaper_un size;
} Backing;

static fontshaper_state* create(TestFont* fonts, unsigned count, unsigned input_capacity,
                                         unsigned glyph_capacity, fontshaper_un cache_size, Backing* backing) {
    fontshaper_font* pointers[3];
    CHECK(count <= 3);
    for (unsigned i = 0; i < count; ++i) pointers[i] = &fonts[i].font;
    fontshaper_state_info info = {pointers, count, input_capacity, glyph_capacity, cache_size};
    backing->size = fontshaper_SizeOfState(&info);
    CHECK(backing->size && backing->size == fontshaper_SizeOfState(&info));
    backing->base = allocate(backing->size);
    fontshaper_state* shaper = fontshaper_CreateState(backing->base, backing->size, &info);
    CHECK(shaper);
    CHECK(!fontshaper_ShapeRunCount(shaper) && !fontshaper_ShapeGlyphCount(shaper));
    return shaper; // The pointer array and descriptor are not borrowed.
}

static Result read_result(fontshaper_state* shaper) {
    Result result = {0};
    fontshaper_state before;
    memcpy(&before, shaper, sizeof(before));
    for (unsigned i = 0; i < fontshaper_ShapeRunCount(shaper); ++i) {
        fontshaper_run run;
        CHECK(fontshaper_ShapeGetRun(shaper, i, &run));
        unsigned count = run.Glyphs.Remaining;
        fontshaper_glyph* glyph;
        while (fontshaper_GlyphIteratorNext(&run.Glyphs, &glyph)) {
            CHECK(result.count < GLYPH_CAPACITY && count);
            --count;
            fontshaper_shape_codepoint input;
            CHECK(fontshaper_ShapeGetShapeCodepoint(shaper, glyph->UserIdOrCodepointIndex, &input));
            CHECK(glyph->SourceIndex == (fontshaper_u32)input.UserId);
            result.glyphs[result.count++] = (Glyph){
                .font = run.Font, .id = glyph->Id, .codepoint = (int)glyph->Codepoint,
                .source = glyph->UserIdOrCodepointIndex, .user_id = (int)glyph->SourceIndex,
                .advance_x = glyph->AdvanceX, .advance_y = glyph->AdvanceY,
                .offset_x = glyph->OffsetX, .offset_y = glyph->OffsetY,
                .direction = run.Direction, .paragraph_direction = run.ParagraphDirection,
            };
        }
        CHECK(!count);
    }
    CHECK(result.count == fontshaper_ShapeGlyphCount(shaper));
    unsigned first = result.count;
    for (unsigned i = fontshaper_ShapeRunCount(shaper); i > 0; --i) {
        fontshaper_run run;
        CHECK(fontshaper_ShapeGetRun(shaper, i - 1, &run));
        CHECK(run.Glyphs.Remaining <= first);
        first -= run.Glyphs.Remaining;
        unsigned at = first;
        fontshaper_glyph* glyph;
        while (fontshaper_GlyphIteratorNext(&run.Glyphs, &glyph)) {
            CHECK(at < result.count && glyph->Id == result.glyphs[at].id);
            CHECK(glyph->SourceIndex == (fontshaper_u32)result.glyphs[at].user_id);
            ++at;
        }
    }
    CHECK(first == 0);
    CHECK(!memcmp(&before, shaper, sizeof(before))); // Indexed reading mutates no state.
    return result;
}

static Result shape_owned(fontshaper_state* shaper, const char* text, fontshaper_language language,
                           const fontshaper_feature_override* features, unsigned count) {
    CHECK(fontshaper_ShapeBegin(shaper, FONTSHAPER_DIRECTION_DONT_KNOW, language) == FONTSHAPER_SHAPE_ERROR_NONE);
    CHECK(!fontshaper_ShapeRunCount(shaper) && !fontshaper_ShapeGlyphCount(shaper));
    for (unsigned i = 0; i < count; ++i) fontshaper_ShapePushFeature(shaper, features[i].Tag, features[i].Value);
    fontshaper_ShapeUtf8(shaper, text, (int)strlen(text), FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    CHECK(fontshaper_ShapeEnd(shaper) == FONTSHAPER_SHAPE_ERROR_NONE);
    CHECK(shaper->NextRun == 0); // End finished execution without consuming the read cursor.
    Result result = read_result(shaper);
    Result again = read_result(shaper);
    same_result(&result, &again);
    CHECK(fontshaper_ShapeEnd(shaper) == FONTSHAPER_SHAPE_ERROR_NONE); // Completion is idempotent.
    return result;
}

static void equivalence(TestFont* fonts, unsigned font_count, fontshaper_un cache_size) {
    Backing a = {0};
    fontshaper_state* shaper = create(fonts, font_count, INPUT_CAPACITY, GLYPH_CAPACITY, cache_size, &a);
    TestState reference = make_test_state(fonts, font_count, cache_size);
    const char* texts[] = {
        "office fi AV", "Cafe\xcc\x81, na\xc3\xafve and office.", "", "first\tline\r\n\r\nlast\n",
        "هذا نص عربي لاختبار تشكيل الحروف.",
        "यह एक छोटा अनुच्छेद है जिसमें संयुक्ताक्षरों का परीक्षण है।",
        "Latin ثم العربية ثم Latin. देवनागरी and office.", "office \xff next", "бгдпт office",
    };
    fontshaper_feature_override features[] = {{FONTSHAPER_FEATURE_TAG_liga, 0}, {FONTSHAPER_FEATURE_TAG_kern, 1}};
    fontshaper_language languages[] = {0, FONTSHAPER_LANGUAGE_TURKISH, FONTSHAPER_FOURCC('S','R','B',' ')};
    for (unsigned l = 0; l < sizeof(languages) / sizeof(*languages); ++l) {
        resize_scratch(&reference, languages[l]);
        CHECK(shaper->StateScratchSize >= reference.scratch_size);
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
    fontshaper_state* shaper = create(fonts, 1, 2, 8, CACHE_BYTES, &a);
    fontshaper_run run;
    fontshaper_ShapeCodepoint(shaper, 'x');
    CHECK(fontshaper_ShapeEnd(shaper) == FONTSHAPER_SHAPE_ERROR_GAVE_TEXT_BEFORE_CALLING_BEGIN);
    CHECK(fontshaper_ShapeBegin(shaper, FONTSHAPER_DIRECTION_LTR, 0) == 0);
    fontshaper_ShapeUtf8(shaper, "office", 6, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    CHECK(fontshaper_ShapeEnd(shaper) == FONTSHAPER_SHAPE_ERROR_INPUT_CAPACITY);
    CHECK(!fontshaper_ShapeRunCount(shaper) && !fontshaper_ShapeGetRun(shaper, 0, &run));
    Result result = shape_owned(shaper, "fi", 0, NULL, 0);
    CHECK(result.count);
    CHECK(fontshaper_ShapeBegin(shaper, FONTSHAPER_DIRECTION_LTR, 0) == 0);
    fontshaper_ShapeUtf8(shaper, NULL, 1, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    CHECK(fontshaper_ShapeEnd(shaper) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    CHECK(fontshaper_ShapeBegin(shaper, 999, 0) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    result = shape_owned(shaper, "fi", 0, NULL, 0);
    CHECK(result.count);
    CHECK(fontshaper_ShapeBegin(shaper, FONTSHAPER_DIRECTION_LTR, 0) == 0);
    fontshaper_ShapeCodepoint(shaper, 0xd800);
    CHECK(fontshaper_ShapeEnd(shaper) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    CHECK(fontshaper_ShapeBegin(shaper, FONTSHAPER_DIRECTION_LTR, 0) == 0);
    fontshaper_ShapeBeginManualRuns(shaper);
    fontshaper_ShapeNextManualRun(shaper, FONTSHAPER_DIRECTION_LTR, (fontshaper_script)-1);
    CHECK(fontshaper_ShapeEnd(shaper) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    result = shape_owned(shaper, "fi", 0, NULL, 0);
    CHECK(result.count);
    fontshaper_ShapeManualBreak(shaper);
    CHECK(fontshaper_ShapeEnd(shaper) == FONTSHAPER_SHAPE_ERROR_GAVE_TEXT_BEFORE_CALLING_BEGIN);
    free(a.base);

    a = (Backing){0};
    shaper = create(fonts, 1, INPUT_CAPACITY, 8, CACHE_BYTES, &a);
    CHECK(fontshaper_ShapeBegin(shaper, FONTSHAPER_DIRECTION_LTR, 0) == 0);
    for (unsigned i = 0; i < 16; ++i) {
        fontshaper_ShapeUtf8(shaper, "fi ", 3, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
        fontshaper_ShapeManualBreak(shaper);
    }
    CHECK(fontshaper_ShapeEnd(shaper) == FONTSHAPER_SHAPE_ERROR_GLYPH_CAPACITY);
    CHECK(!fontshaper_ShapeRunCount(shaper) && !fontshaper_ShapeGlyphCount(shaper));
    CHECK(!fontshaper_ShapeRun(shaper, &run)); // A later run's failure is already reported.
    result = shape_owned(shaper, "fi", 0, NULL, 0);
    CHECK(result.count);
    fontshaper_un cache_size = shaper->CacheMemory.Used;
    free(a.base);

    a = (Backing){0};
    shaper = create(fonts, 1, INPUT_CAPACITY, 8, cache_size, &a);
    result = shape_owned(shaper, "fi", 0, NULL, 0);
    CHECK(result.count && shaper->CacheMemory.Used == cache_size);
    CHECK(fontshaper_ShapeBegin(shaper, FONTSHAPER_DIRECTION_LTR, 0) == 0);
    fontshaper_ShapePushFeature(shaper, FONTSHAPER_FEATURE_TAG_liga, 0);
    fontshaper_ShapeUtf8(shaper, "fi", 2, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    CHECK(fontshaper_ShapeEnd(shaper) == FONTSHAPER_SHAPE_ERROR_CACHE_CAPACITY);
    CHECK(!fontshaper_ShapeRunCount(shaper) && !fontshaper_ShapeRun(shaper, &run));
    result = shape_owned(shaper, "fi", 0, NULL, 0); // Failed cache construction resets before retry.
    CHECK(result.count);
    free(a.base);
}

static void construction(TestFont* fonts) {
    fontshaper_font* pointers[] = {&fonts[0].font};
    fontshaper_state_info info = {pointers, 1, INPUT_CAPACITY, GLYPH_CAPACITY, CACHE_BYTES};
    CHECK(!fontshaper_SizeOfState(NULL));
    CHECK(!fontshaper_CreateState(NULL, 0, &info));
    CHECK(!fontshaper_CreateState(NULL, (fontshaper_un)-1, NULL));
    info.FontCount = 0;
    CHECK(!fontshaper_SizeOfState(&info));
    info.FontCount = FONTSHAPER_STATE_MAX_FONT_COUNT + 1;
    CHECK(!fontshaper_SizeOfState(&info));
    info.FontCount = 1;
    info.CacheSize = (fontshaper_un)-1;
    CHECK(!fontshaper_SizeOfState(&info));
    info.CacheSize = CACHE_BYTES;
    info.CodepointCapacity = 0;
    CHECK(!fontshaper_SizeOfState(&info));
    info.CodepointCapacity = 1u << 27;
    CHECK(!fontshaper_SizeOfState(&info));
    info.CodepointCapacity = INPUT_CAPACITY;
    info.GlyphCapacity = (fontshaper_u32)-1;
    CHECK(!fontshaper_SizeOfState(&info));
    info.GlyphCapacity = 0;
    CHECK(!fontshaper_SizeOfState(&info));
    info.GlyphCapacity = GLYPH_CAPACITY;
    fontshaper_font invalid = {0};
    pointers[0] = &invalid;
    CHECK(!fontshaper_SizeOfState(&info));
    pointers[0] = &fonts[0].font;
    fontshaper_un size = fontshaper_SizeOfState(&info);
    CHECK(size && size == fontshaper_SizeOfState(&info));
    unsigned alignment = FONTSHAPER_ALIGNOF(fontshaper_state);
    // Exact query-sized storage works for every base alignment. Guard bytes
    // catch partition overflow; creation failures must not touch the block.
    unsigned char* allocation = allocate(size + 2 * alignment);
    for (unsigned offset = 0; offset < alignment; ++offset) {
        memset(allocation, 0xa5, (size_t)size + 2 * alignment);
        unsigned char* memory = allocation + alignment + offset;
        CHECK(!fontshaper_CreateState(memory, size - 1, &info));
        CHECK(!fontshaper_CreateState(memory, size, NULL));
        pointers[0] = &invalid;
        CHECK(!fontshaper_CreateState(memory, size, &info));
        pointers[0] = &fonts[0].font;
        for (fontshaper_un i = 0; i < size + 2 * alignment; ++i) CHECK(allocation[i] == 0xa5);
        fontshaper_state* shaper = fontshaper_CreateState(memory, size, &info);
        CHECK(shaper && !((uintptr_t)shaper & (alignment - 1)));
        CHECK(!fontshaper_SetStateCache(shaper, NULL, 0));
        CHECK(!fontshaper_ShapePushFont(shaper, &fonts[0].font));
        CHECK(!fontshaper_ShapePopFont(shaper));
        pointers[0] = NULL; // The caller's font-pointer array was copied.
        CHECK(shape_status(shaper, "office fi", 9, (fontshaper_shape_options){0}) == 0);
        CHECK(fontshaper_ShapeGlyphCount(shaper));
        fontshaper_ShapeBeginWithMemory(shaper, 0, 0, NULL, NULL, 0, 0);
        CHECK(fontshaper_ShapeError(shaper) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
        CHECK(shape_status(shaper, "fi", 2, (fontshaper_shape_options){0}) == 0);
        pointers[0] = &fonts[0].font;
        shaper = fontshaper_CreateState(memory, size, &info); // Reinitialize, no destroy.
        CHECK(shaper && shape_status(shaper, "fi", 2, (fontshaper_shape_options){0}) == 0);
        for (unsigned i = 0; i < alignment + offset; ++i) CHECK(allocation[i] == 0xa5);
        for (fontshaper_un i = alignment + offset + size; i < size + 2 * alignment; ++i)
            CHECK(allocation[i] == 0xa5);
    }
    free(allocation); // Library never owns or releases the backing block.

    TestState placed = make_test_state(fonts, 1, CACHE_BYTES);
    fontshaper_ShapeBeginWithMemory(placed.shaper, FONTSHAPER_DIRECTION_LTR, 0, placed.destination,
                                   placed.scratch[0], INPUT_CAPACITY, GLYPH_CAPACITY);
    CHECK(!fontshaper_SetStateCache(placed.shaper, placed.cache, CACHE_BYTES));
    fontshaper_ShapeUtf8(placed.shaper, "fi", 2, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    CHECK(fontshaper_ShapeEnd(placed.shaper) == 0 && fontshaper_ShapeRunCount(placed.shaper));
    CHECK(fontshaper_SetStateCache(placed.shaper, placed.cache, CACHE_BYTES));
    fontshaper_run run;
    CHECK(!fontshaper_ShapeRunCount(placed.shaper) && !fontshaper_ShapeGetRun(placed.shaper, 0, &run));
    free_test_state(&placed);
}

static void check_flat(fontshaper_state* state, const Result* expected) {
    fontshaper_un scratch_used = state->PassMemory.Used, cache_used = state->CacheMemory.Used;
    unsigned i = 0;
    fontshaper_glyph_view view;
    while (fontshaper_NextGlyph(state, &view)) {
        CHECK(i < expected->count);
        const Glyph* old = &expected->glyphs[i++];
        CHECK(view.Font == old->font && view.Glyph->Id == old->id);
        CHECK(view.Glyph->SourceIndex == (fontshaper_u32)old->user_id);
        CHECK(view.Glyph->UserIdOrCodepointIndex == old->source);
        CHECK(view.Glyph->AdvanceX == old->advance_x && view.Glyph->AdvanceY == old->advance_y);
        CHECK(view.Glyph->OffsetX == old->offset_x && view.Glyph->OffsetY == old->offset_y);
        CHECK(view.Direction == old->direction && view.ParagraphDirection == old->paragraph_direction);
        fontshaper_run run;
        CHECK(fontshaper_ShapeGetRun(state, state->ReadRun - 1, &run));
        CHECK(view.Script == run.Script && view.Flags == run.Flags);
    }
    CHECK(i == expected->count && !fontshaper_NextGlyph(state, &view));
    CHECK(state->PassMemory.Used == scratch_used && state->CacheMemory.Used == cache_used);
    CHECK(fontshaper_ShapeError(state) == 0);
}

static void same_metadata(fontshaper_state* a, fontshaper_state* b) {
    CHECK(a->InputCodepointCount == b->InputCodepointCount);
    CHECK(fontshaper_ShapeRunCount(a) == fontshaper_ShapeRunCount(b));
    for (unsigned i = 0; i <= a->InputCodepointCount; ++i) {
        fontshaper_shape_codepoint* x = fontshaper__InputCodepoint(a, i, 0);
        fontshaper_shape_codepoint* y = fontshaper__InputCodepoint(b, i, 0);
        CHECK(x->Codepoint == y->Codepoint && x->UserId == y->UserId);
        CHECK(x->BreakFlags == y->BreakFlags && x->Script == y->Script);
        CHECK(x->Direction == y->Direction && x->ParagraphDirection == y->ParagraphDirection);
        CHECK(x->Font == y->Font && x->MappedFont == y->MappedFont && x->MappedGlyphId == y->MappedGlyphId);
        CHECK(x->FeatureOverrideCount == y->FeatureOverrideCount);
        for (unsigned f = 0; f < x->FeatureOverrideCount; ++f) {
            CHECK(x->FeatureOverrides[f].Tag == y->FeatureOverrides[f].Tag);
            CHECK(x->FeatureOverrides[f].Value == y->FeatureOverrides[f].Value);
        }
    }
    for (unsigned i = 0; i < fontshaper_ShapeRunCount(a); ++i) {
        fontshaper_run x, y;
        CHECK(fontshaper_ShapeGetRun(a, i, &x) && fontshaper_ShapeGetRun(b, i, &y));
        CHECK(x.Font == y.Font && x.Script == y.Script && x.Direction == y.Direction);
        CHECK(x.ParagraphDirection == y.ParagraphDirection && x.Flags == y.Flags);
        CHECK(a->RunBoundaries[i] == b->RunBoundaries[i] && a->RunBoundaries[i + 1] == b->RunBoundaries[i + 1]);
        CHECK(x.Glyphs.Remaining == y.Glyphs.Remaining);
    }
}

static void batch_equivalence(TestFont* fonts, fontshaper_un cache_size) {
    Backing a = {0}, b = {0};
    fontshaper_state* batch = create(fonts, 3, INPUT_CAPACITY, GLYPH_CAPACITY, cache_size, &a);
    fontshaper_state* incremental = create(fonts, 3, INPUT_CAPACITY, GLYPH_CAPACITY, cache_size, &b);
    const char* texts[] = {
        "", "office fi AV", "Cafe\xcc\x81 and office.", "first\tline\r\n\r\nlast\n",
        "هذا نص عربي لاختبار تشكيل الحروف.",
        "यह एक छोटा अनुच्छेद है जिसमें संयुक्ताक्षरों का परीक्षण है।",
        "Latin ثم العربية ثم Latin. देवनागरी and office.",
        "\xff" "a\x80" "b\xe2\x82" "c\xc2", "f\0i",
    };
    fontshaper_feature_override off[] = {{FONTSHAPER_FEATURE_TAG_liga, 0}, {FONTSHAPER_FEATURE_TAG_kern, 1}};
    fontshaper_feature_override duplicates[] = {
        {FONTSHAPER_FEATURE_TAG_liga, 1}, {FONTSHAPER_FEATURE_TAG_kern, 1}, {FONTSHAPER_FEATURE_TAG_liga, 0},
        {FONTSHAPER_FEATURE_TAG_salt, 2},
    };
    for (unsigned lang = 0; lang < 2; ++lang) {
        for (unsigned direction = 0; direction < 3; ++direction) {
            for (unsigned style = 0; style < 3; ++style) {
                fontshaper_shape_options options = {.Direction = direction};
                if (lang) options.Language = FONTSHAPER_LANGUAGE_TURKISH;
                if (style == 1) { options.Features = off; options.FeatureCount = 2; }
                if (style == 2) { options.Features = duplicates; options.FeatureCount = 4; }
                for (unsigned t = 0; t < sizeof(texts) / sizeof(*texts); ++t) {
                    unsigned length = (unsigned)strlen(texts[t]);
                    if (t == 8) length = 3; // Embedded NUL, not a string terminator.
                    CHECK(shape_status(batch, texts[t], length, options) == 0);
                    CHECK(fontshaper_ShapeBegin(incremental, options.Direction, options.Language) == 0);
                    for (unsigned f = 0; f < options.FeatureCount; ++f)
                        fontshaper_ShapePushFeature(incremental, options.Features[f].Tag, options.Features[f].Value);
                    // Unlike plain legacy ShapeUtf8, this advanced append accounts
                    // for skipped malformed bytes, making the mapping oracle explicit.
                    fontshaper_ShapeUtf8WithUserId(incremental, texts[t], (int)length, 0,
                                                  FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
                    CHECK(fontshaper_ShapeEnd(incremental) == 0);
                    Result expected = read_result(incremental), actual = read_result(batch);
                    same_result(&expected, &actual);
                    same_metadata(batch, incremental);
                    check_flat(batch, &actual);
                    check_flat(incremental, &expected);
                }
            }
        }
    }
    fontshaper_shape_options options = {.Features = off, .FeatureCount = 2};
    CHECK(shape_status(batch, "office fi", 9, options) == 0);
    fontshaper_un used = batch->CacheMemory.Used;
    fontshaper_glyph_config* config = batch->PreparedGlyphConfig;
    fontshaper_feature_override reordered[] = {off[1], off[0]};
    options.Features = reordered;
    memset(batch->StateScratch, 0xa5, (size_t)batch->StateScratchSize);
    CHECK(shape_status(batch, "office fi", 9, options) == 0);
    CHECK(batch->CacheMemory.Used == used);
    if (cache_size) CHECK(batch->PreparedGlyphConfig == config);
    reordered[1].Value = 1;
    CHECK(shape_status(batch, "office fi", 9, options) == 0);
    CHECK(fontshaper_ShapeGlyphCount(batch) < 9);
    char text[INPUT_CAPACITY];
    memset(text, 'x', sizeof(text));
    for (unsigned n = 126; n <= INPUT_CAPACITY; ++n) {
        CHECK(shape_status(batch, text, n, (fontshaper_shape_options){0}) == 0);
        CHECK(batch->InputCodepointCount == n && fontshaper_ShapeGlyphCount(batch) == n);
        for (unsigned i = 0; i < n; ++i) {
            fontshaper_shape_codepoint input;
            CHECK(fontshaper_ShapeGetShapeCodepoint(batch, i, &input) && input.UserId == (int)i);
        }
    }
    // Source IDs use actual byte positions; the decoder's skip policy is unchanged.
    CHECK(shape_status(batch, "\x80" "a\x80" "b", 4, (fontshaper_shape_options){0}) == 0);
    fontshaper_shape_codepoint input;
    CHECK(batch->InputCodepointCount == 2);
    CHECK(fontshaper_ShapeGetShapeCodepoint(batch, 0, &input) && input.UserId == 1);
    CHECK(fontshaper_ShapeGetShapeCodepoint(batch, 1, &input) && input.UserId == 3);
    free(a.base);
    free(b.base);
}

static void batch_failures(TestFont* fonts) {
    Backing a = {0};
    fontshaper_state* shaper = create(fonts, 1, 2, 8, CACHE_BYTES, &a);
    fontshaper_shape_options defaults = {0};
    CHECK(shape_status(NULL, "", 0, defaults) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    CHECK(fontshaper_Shape(shaper, "fi", 2, defaults) == 1);
    Result flat_expected = read_result(shaper);
    check_flat(shaper, &flat_expected);
    CHECK(shape_status(shaper, NULL, 1, defaults) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    CHECK(!fontshaper_ShapeRunCount(shaper) && !fontshaper_ShapeGlyphCount(shaper));
    fontshaper_glyph_view failed_view;
    CHECK(!fontshaper_NextGlyph(shaper, &failed_view));
    CHECK(shape_status(shaper, NULL, 0, defaults) == 0);
    CHECK(shape_status(shaper, "abc", 3, defaults) == FONTSHAPER_SHAPE_ERROR_INPUT_CAPACITY);
    CHECK(!fontshaper_ShapeRunCount(shaper));
    // Byte length may exceed input capacity: only decoded records consume it.
    CHECK(shape_status(shaper, "\xc3\xa9\xc3\xa9", 4, defaults) == 0);
    CHECK(shaper->InputCodepointCount == 2);
    CHECK(shape_status(shaper, "\xff\xff\xff", 3, defaults) == 0);
    CHECK(shaper->InputCodepointCount == 0);
    CHECK(shape_status(shaper, "\xed\xa0\x80", 3, defaults) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    CHECK(shape_status(shaper, "\xf4\x90\x80\x80", 4, defaults) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    fontshaper_shape_options invalid = {.Direction = 999};
    CHECK(shape_status(shaper, "fi", 2, invalid) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    invalid = (fontshaper_shape_options){.FeatureCount = 1};
    CHECK(shape_status(shaper, "fi", 2, invalid) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    fontshaper_feature_override features[FONTSHAPER_MAX_SIMULTANEOUS_FEATURES + 1];
    for (unsigned i = 0; i < sizeof(features) / sizeof(*features); ++i)
        features[i] = (fontshaper_feature_override){i + 1, 0};
    invalid = (fontshaper_shape_options){.Features = features, .FeatureCount = sizeof(features) / sizeof(*features)};
    CHECK(shape_status(shaper, "fi", 2, invalid) == FONTSHAPER_SHAPE_ERROR_INVALID_ARGUMENT);
    for (unsigned i = 0; i < sizeof(features) / sizeof(*features); ++i)
        features[i] = (fontshaper_feature_override){FONTSHAPER_FEATURE_TAG_liga, 0};
    CHECK(shape_status(shaper, "fi", 2, invalid) == 0); // Raw count > limit, only one distinct tag.
    Result off = read_result(shaper);
    fontshaper_un used = shaper->CacheMemory.Used;
    features[0].Value = 1; // Earlier duplicate loses; cache keys do not borrow this array.
    CHECK(shape_status(shaper, "fi", 2, invalid) == 0 && shaper->CacheMemory.Used == used);
    features[0].Tag = FONTSHAPER_FEATURE_TAG_salt;
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
    CHECK(!fontshaper_ShapeGlyphCount(shaper));
    CHECK(shape_status(shaper, "fi", 2, defaults) == 0);
    fontshaper_un exact = shaper->CacheMemory.Used;
    free(a.base);
    a = (Backing){0};
    shaper = create(fonts, 1, INPUT_CAPACITY, 8, exact, &a);
    CHECK(shape_status(shaper, "fi", 2, defaults) == 0);
    fontshaper_feature_override disable = {FONTSHAPER_FEATURE_TAG_liga, 0};
    fontshaper_shape_options options = {.Features = &disable, .FeatureCount = 1};
    CHECK(shape_status(shaper, "fi", 2, options) == FONTSHAPER_SHAPE_ERROR_CACHE_CAPACITY);
    CHECK(!fontshaper_ShapeRunCount(shaper));
    CHECK(shape_status(shaper, "fi", 2, defaults) == 0);
    free(a.base);
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
    for (unsigned i = 0; i < 3; ++i) { free(fonts[i].compiled); free(fonts[i].blob); }
    puts("fontshaper caller storage: deterministic size, exact/unaligned bounds, eager results, passive rereads, language bounds, whole-buffer/options/byte offsets, failure/retry and lifetime checks passed");
    return 0;
}
