/* From the repository root:
 * clang -O2 -I src tests/fontshaper_context_test.c -lm -o /tmp/fontshaper-context-test
 * /tmp/fontshaper-context-test [Latin-font [Arabic-font [Devanagari-font]]]
 * Add -g -fsanitize=address,undefined for the lifetime/bounds checks.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FONTSHAPER_IMPLEMENTATION
#ifndef FONTSHAPER_TEST_HEADER
#define FONTSHAPER_TEST_HEADER "lib/fontshaper/fontshaper.h"
#endif
#include FONTSHAPER_TEST_HEADER

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define INPUT_CAPACITY 512
#define GLYPH_CAPACITY 1024
#define CACHE_BYTES (8 * 1024 * 1024)

typedef struct TestFont {
    fontshaper_font font;
    void* blob;
    void* compiled;
} TestFont;

typedef struct TestContext {
    fontshaper_shape_context* context;
    void* memory;
    void* destination;
    void* scratch[2];
    void* cache;
    fontshaper_un scratch_size;
    fontshaper_un input_capacity, glyph_capacity;
    unsigned pass;
} TestContext;

typedef struct Glyph {
    fontshaper_font* font;
    int id, codepoint, source, user_id, advance_x, advance_y, offset_x, offset_y;
    int direction, paragraph_direction;
} Glyph;

typedef struct Result {
    unsigned count;
    Glyph glyphs[GLYPH_CAPACITY];
} Result;

static void* allocate(fontshaper_un bytes) {
    CHECK(bytes && bytes <= SIZE_MAX);
    void* result = malloc((size_t)bytes);
    CHECK(result);
    return result;
}

static TestFont load_font(const char* path) {
    FILE* file = fopen(path, "rb");
    CHECK(file);
    CHECK(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    CHECK(length > 0 && length <= INT32_MAX);
    rewind(file);
    void* bytes = allocate((fontshaper_un)length);
    CHECK(fread(bytes, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    TestFont result = {0};
    fontshaper_load_font_state state = {0};
    int scratch_size = 0, blob_size = 0;
    CHECK(fontshaper_LoadFont(&result.font, &state, bytes, (int)length, 0, &scratch_size, &blob_size) ==
          FONTSHAPER_LOAD_FONT_ERROR_NEED_TO_CREATE_BLOB);
    void* scratch = allocate((fontshaper_un)scratch_size);
    result.blob = allocate((fontshaper_un)blob_size);
    CHECK(fontshaper_PlaceBlob(&result.font, &state, scratch, result.blob) == FONTSHAPER_LOAD_FONT_ERROR_NONE);
    free(scratch);
    free(bytes);
    result.compiled = allocate(fontshaper_SizeOfCompiledFont(&result.font));
    scratch = allocate(fontshaper_SizeOfFontCompileScratch(&result.font));
    CHECK(fontshaper_CompileFont(&result.font, result.compiled, scratch, NULL) == FONTSHAPER_LOAD_FONT_ERROR_NONE);
    free(scratch);
    return result;
}

static void resize_scratch(TestContext* state, fontshaper_language language) {
    fontshaper_un size = fontshaper_SizeOfContextScratch(state->context, language,
                                                        state->input_capacity, state->glyph_capacity);
    CHECK(size);
    if (size == state->scratch_size) return;
    state->scratch_size = size;
    for (unsigned i = 0; i < 2; ++i) {
        free(state->scratch[i]);
        state->scratch[i] = allocate(size);
    }
}

static TestContext make_context(TestFont* fonts, unsigned font_count, fontshaper_un cache_size) {
    TestContext result = {0};
    result.input_capacity = INPUT_CAPACITY;
    result.glyph_capacity = GLYPH_CAPACITY;
    result.memory = allocate((fontshaper_un)fontshaper_SizeOfShapeContext());
    result.context = fontshaper_PlaceShapeContext(result.memory, 0);
    CHECK(result.context);
    for (unsigned i = 0; i < font_count; ++i)
        CHECK(fontshaper_ShapePushFont(result.context, &fonts[i].font) == &fonts[i].font);
    result.destination = allocate(fontshaper_SizeOfGlyphStorage(GLYPH_CAPACITY));
    if (cache_size) {
        result.cache = allocate(cache_size);
        CHECK(fontshaper_SetContextCache(result.context, result.cache, cache_size));
    }
    resize_scratch(&result, FONTSHAPER_LANGUAGE_DONT_KNOW);
    return result;
}

static void destroy_context(TestContext* context) {
    free(context->cache);
    free(context->scratch[1]);
    free(context->scratch[0]);
    free(context->destination);
    free(context->memory);
}

static Result finish(TestContext* state) {
    fontshaper_shape_context* context = state->context;
    fontshaper_ShapeEnd(context);
    CHECK(fontshaper_ShapeError(context) == FONTSHAPER_SHAPE_ERROR_NONE);
    Result result = {0};
    fontshaper_run run;
    while (fontshaper_ShapeRun(context, &run)) {
        fontshaper_glyph* glyph;
        while (fontshaper_GlyphIteratorNext(&run.Glyphs, &glyph)) {
            CHECK(result.count < GLYPH_CAPACITY);
            fontshaper_shape_codepoint input;
            CHECK(fontshaper_ShapeGetShapeCodepoint(context, glyph->UserIdOrCodepointIndex, &input));
            result.glyphs[result.count++] = (Glyph){
                .font = run.Font, .id = glyph->Id, .codepoint = (int)glyph->Codepoint,
                .source = glyph->UserIdOrCodepointIndex, .user_id = input.UserId,
                .advance_x = glyph->AdvanceX, .advance_y = glyph->AdvanceY,
                .offset_x = glyph->OffsetX, .offset_y = glyph->OffsetY,
                .direction = run.Direction, .paragraph_direction = run.ParagraphDirection,
            };
        }
    }
    CHECK(fontshaper_ShapeError(context) == FONTSHAPER_SHAPE_ERROR_NONE);
    /* Destroy the just-used transient data and alternate backing buffers. Cached
     * keys/configs must not point at either paragraph scratch allocation. */
    CHECK(context->PassMemory.Capacity == state->scratch_size);
    CHECK(context->PassMemory.Used <= state->scratch_size);
    memset(context->PassMemory.Data, 0xa5, (size_t)context->PassMemory.Used);
    return result;
}

static Result shape(TestContext* state, const char* text, fontshaper_language language,
                    fontshaper_feature_override* features, unsigned feature_count) {
    fontshaper_shape_context* context = state->context;
    for (unsigned i = 0; i < feature_count; ++i)
        fontshaper_ShapePushFeature(context, features[i].Tag, features[i].Value);
    resize_scratch(state, language);
    fontshaper_ShapeBegin(context, FONTSHAPER_DIRECTION_DONT_KNOW, language,
                         state->destination, state->scratch[state->pass++ & 1], state->input_capacity, state->glyph_capacity);
    fontshaper_ShapeUtf8(context, text, (int)strlen(text), FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    Result result = finish(state);
    for (unsigned i = feature_count; i > 0; --i)
        CHECK(fontshaper_ShapePopFeature(context, features[i - 1].Tag));
    return result;
}

static Result mixed_styles(TestContext* state) {
    fontshaper_shape_context* context = state->context;
    fontshaper_ShapeBegin(context, FONTSHAPER_DIRECTION_LTR, 0, state->destination,
                         state->scratch[state->pass++ & 1], INPUT_CAPACITY, GLYPH_CAPACITY);
    for (unsigned span = 0; span < 4; ++span) {
        fontshaper_ShapePushFeature(context, FONTSHAPER_FEATURE_TAG_liga, (int)(span & 1));
        fontshaper_ShapeUtf8(context, "office fi AV ", 13, FONTSHAPER_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);
        CHECK(fontshaper_ShapePopFeature(context, FONTSHAPER_FEATURE_TAG_liga));
        fontshaper_ShapeManualBreak(context);
    }
    return finish(state);
}

static void same_result(const Result* left, const Result* right) {
    CHECK(left->count == right->count);
    for (unsigned i = 0; i < left->count; ++i) {
        const Glyph* a = &left->glyphs[i];
        const Glyph* b = &right->glyphs[i];
        CHECK(a->font == b->font && a->id == b->id && a->codepoint == b->codepoint);
        CHECK(a->source == b->source && a->user_id == b->user_id);
        CHECK(a->advance_x == b->advance_x && a->advance_y == b->advance_y);
        CHECK(a->offset_x == b->offset_x && a->offset_y == b->offset_y);
        CHECK(a->direction == b->direction && a->paragraph_direction == b->paragraph_direction);
    }
}

static void compare(TestContext* cached, TestContext* reference, const char* text,
                    fontshaper_language language, fontshaper_feature_override* features, unsigned count) {
    Result actual = shape(cached, text, language, features, count);
    Result expected = shape(reference, text, language, features, count);
    same_result(&actual, &expected);
}

static void configurations(TestFont* fonts) {
    TestContext cached = make_context(fonts, 1, CACHE_BYTES);
    TestContext reference = make_context(fonts, 1, 0);
    fontshaper_feature_override off[] = {{FONTSHAPER_FEATURE_TAG_liga, 0}, {FONTSHAPER_FEATURE_TAG_kern, 1}};
    fontshaper_feature_override reordered[] = {{FONTSHAPER_FEATURE_TAG_kern, 1}, {FONTSHAPER_FEATURE_TAG_liga, 0}};
    fontshaper_feature_override on[] = {{FONTSHAPER_FEATURE_TAG_liga, 1}, {FONTSHAPER_FEATURE_TAG_kern, 0}};
    fontshaper_feature_override last_wins[] = {{FONTSHAPER_FEATURE_TAG_liga, 1},
        {FONTSHAPER_FEATURE_TAG_kern, 1}, {FONTSHAPER_FEATURE_TAG_liga, 0}};
    fontshaper_feature_override nonbinary[] = {{FONTSHAPER_FEATURE_TAG_salt, 2}};
    const char* text = "The efficient office staff finished fifty files. AV To fi ffi.";
    compare(&cached, &reference, text, 0, off, 2);
    fontshaper_un used = cached.context->CacheMemory.Used;
    fontshaper_shape_config* shape_config = cached.context->PreparedShapeConfig;
    fontshaper_glyph_config* glyph_config = cached.context->PreparedGlyphConfig;
    CHECK(shape_config && glyph_config);
    compare(&cached, &reference, text, 0, reordered, 2);
    CHECK(cached.context->CacheMemory.Used == used);
    CHECK(cached.context->PreparedShapeConfig == shape_config);
    CHECK(cached.context->PreparedGlyphConfig == glyph_config);
    compare(&cached, &reference, text, 0, last_wins, 3);
    CHECK(cached.context->CacheMemory.Used == used);
    compare(&cached, &reference, text, 0, on, 2);
    CHECK(cached.context->PreparedGlyphConfig != glyph_config);
    compare(&cached, &reference, text, 0, nonbinary, 1);
    compare(&cached, &reference, text, FONTSHAPER_FOURCC('T', 'R', 'K', ' '), NULL, 0);
    compare(&cached, &reference, "Cafe\xcc\x81, na\xc3\xafve, AV fi.", 0, NULL, 0);
    compare(&cached, &reference, "", 0, NULL, 0);
    char full[INPUT_CAPACITY + 1];
    memset(full, 'a', INPUT_CAPACITY);
    full[INPUT_CAPACITY] = 0;
    compare(&cached, &reference, full, 0, NULL, 0);
    compare(&cached, &reference, "First line\n\nSecond line\r\n", 0, NULL, 0);
    Result styled = mixed_styles(&cached);
    Result styled_reference = mixed_styles(&reference);
    same_result(&styled, &styled_reference);
    for (unsigned i = 0; i < 40; ++i) {
        fontshaper_feature_override value = {FONTSHAPER_FEATURE_TAG_salt, (int)i + 10};
        compare(&cached, &reference, text, 0, &value, 1);
    }
    used = cached.context->CacheMemory.Used;
    for (unsigned i = 0; i < 40; ++i) {
        fontshaper_feature_override value = {FONTSHAPER_FEATURE_TAG_salt, (int)i + 10};
        compare(&cached, &reference, text, 0, &value, 1);
        CHECK(cached.context->CacheMemory.Used == used);
    }
    cached.input_capacity = 128;
    cached.glyph_capacity = 256;
    compare(&cached, &reference, text, 0, reordered, 2);
    CHECK(cached.context->CachedCodepointCapacity == 128 && cached.context->CachedGlyphCapacity == 256);
    CHECK(cached.context->CacheMemory.Used == used);
    cached.input_capacity = INPUT_CAPACITY;
    cached.glyph_capacity = GLYPH_CAPACITY;
    for (unsigned i = 0; i < 100; ++i) {
        compare(&cached, &reference, text, 0, reordered, 2);
        CHECK(cached.context->CacheMemory.Used == used);
    }
    fontshaper_un small = cached.scratch_size;
    CHECK(fontshaper_SetContextCache(cached.context, NULL, 0));
    CHECK(!cached.context->CachedScratchSize);
    compare(&cached, &reference, text, 0, off, 2);
    CHECK(cached.scratch_size == reference.scratch_size && cached.scratch_size > small);
    CHECK(fontshaper_SetContextCache(cached.context, cached.cache, CACHE_BYTES));
    CHECK(!cached.context->CachedScratchSize);
    compare(&cached, &reference, text, 0, off, 2);
    CHECK(cached.scratch_size == small);
    CHECK(!fontshaper_SetContextCache(cached.context, NULL, 1));
    CHECK(!fontshaper_SetContextCache(cached.context, cached.cache, 0));
    CHECK(fontshaper_SetContextCache(cached.context, (char*)cached.cache + 1, CACHE_BYTES - 1));
    CHECK(!cached.context->CachedScratchSize);
    compare(&cached, &reference, text, 0, on, 2);
    CHECK(cached.scratch_size == small);
    destroy_context(&reference);
    destroy_context(&cached);
}

static void bounds_and_fonts(TestFont* fonts) {
    TestContext state = make_context(fonts, 1, CACHE_BYTES);
    fontshaper_shape_context* context = state.context;
    fontshaper_un original = context->CachedScratchSize;
    CHECK(original == state.scratch_size);
    CHECK(fontshaper_SizeOfContextScratch(context, 0, INPUT_CAPACITY, GLYPH_CAPACITY) == original);
    CHECK(fontshaper_SizeOfContextScratch(context, 0, INPUT_CAPACITY + 1, GLYPH_CAPACITY));
    CHECK(context->CachedCodepointCapacity == INPUT_CAPACITY + 1);
    CHECK(fontshaper_SizeOfContextScratch(context, 0, INPUT_CAPACITY, GLYPH_CAPACITY + 1));
    CHECK(context->CachedGlyphCapacity == GLYPH_CAPACITY + 1);
    fontshaper_language language = FONTSHAPER_FOURCC('T', 'R', 'K', ' ');
    CHECK(fontshaper_SizeOfContextScratch(context, language, INPUT_CAPACITY, GLYPH_CAPACITY));
    CHECK(context->CachedScratchLanguage == language);
    CHECK(!fontshaper_SizeOfContextScratch(context, 0, (fontshaper_un)1 << 27, GLYPH_CAPACITY));
    CHECK(!fontshaper_SizeOfContextScratch(context, 0, INPUT_CAPACITY, (fontshaper_un)-1));
    Result before = shape(&state, "office AV", 0, NULL, 0);
    CHECK(context->CacheMemory.Used);
    CHECK(fontshaper_ShapePushFont(context, &fonts[0].font));
    CHECK(!context->CachedScratchSize && context->ConfigCacheDirty);
    CHECK(fontshaper_ShapePopFont(context) == &fonts[0].font);
    CHECK(!context->CachedScratchSize);
    Result after = shape(&state, "office AV", 0, NULL, 0);
    same_result(&before, &after);
    CHECK(!context->ConfigCacheDirty);
    CHECK(fontshaper_ShapePushFont(context, &fonts[1].font));
    Result arabic = shape(&state, "مرحبا بالعالم", 0, NULL, 0);
    CHECK(arabic.count);
    for (unsigned i = 0; i < arabic.count; ++i) CHECK(arabic.glyphs[i].font == &fonts[1].font);
    CHECK(fontshaper_ShapePopFont(context) == &fonts[1].font);
    after = shape(&state, "office AV", 0, NULL, 0);
    same_result(&before, &after);
    fontshaper_font invalid = {0};
    CHECK(fontshaper_ShapePushFont(context, &invalid) == &invalid);
    CHECK(!fontshaper_SizeOfContextScratch(context, 0, INPUT_CAPACITY, GLYPH_CAPACITY));
    CHECK(fontshaper_ShapePopFont(context) == &invalid);
    CHECK(fontshaper_SizeOfContextScratch(context, 0, INPUT_CAPACITY, GLYPH_CAPACITY) == original);
    fontshaper_un exact_size = context->CacheMemory.Used;
    void* exact = allocate(exact_size);
    CHECK(fontshaper_SetContextCache(context, exact, exact_size));
    after = shape(&state, "office AV", 0, NULL, 0);
    same_result(&before, &after);
    CHECK(context->CacheMemory.Used == exact_size);
    fontshaper_ShapePushFeature(context, FONTSHAPER_FEATURE_TAG_liga, 0);
    fontshaper_ShapeBegin(context, FONTSHAPER_DIRECTION_LTR, 0, state.destination,
                         state.scratch[0], INPUT_CAPACITY, GLYPH_CAPACITY);
    fontshaper_ShapeUtf8(context, "office", 6, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    fontshaper_ShapeEnd(context);
    CHECK(fontshaper_ShapeError(context) == FONTSHAPER_SHAPE_ERROR_OUT_OF_MEMORY);
    CHECK(context->CacheMemory.Used <= exact_size);
    free(exact);
    destroy_context(&state);

    TestContext tiny = make_context(fonts, 1, 1);
    fontshaper_ShapeBegin(tiny.context, FONTSHAPER_DIRECTION_LTR, 0, tiny.destination,
                         tiny.scratch[0], INPUT_CAPACITY, GLYPH_CAPACITY);
    fontshaper_ShapeUtf8(tiny.context, "office", 6, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    fontshaper_ShapeEnd(tiny.context);
    CHECK(fontshaper_ShapeError(tiny.context) == FONTSHAPER_SHAPE_ERROR_OUT_OF_MEMORY);
    fontshaper_run run;
    CHECK(!fontshaper_ShapeRun(tiny.context, &run));
    CHECK(!fontshaper_SetContextCache(tiny.context, NULL, 0));
    destroy_context(&tiny);

    TestContext limited = make_context(fonts, 1, CACHE_BYTES);
    fontshaper_ShapeBegin(limited.context, FONTSHAPER_DIRECTION_LTR, 0, limited.destination,
                         limited.scratch[0], INPUT_CAPACITY, 1);
    fontshaper_ShapeUtf8(limited.context, "AV", 2, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    fontshaper_ShapeEnd(limited.context);
    while (fontshaper_ShapeRun(limited.context, &run)) {}
    CHECK(fontshaper_ShapeError(limited.context) == FONTSHAPER_SHAPE_ERROR_OUT_OF_MEMORY);
    destroy_context(&limited);
}

static void multilingual(TestFont* fonts) {
    TestContext cached = make_context(fonts, 3, CACHE_BYTES);
    TestContext reference = make_context(fonts, 3, 0);
    const char* paragraphs[] = {
        "Latin, Ελληνικά, русский текст. Cafe\xcc\x81 and office.",
        "هذا نص عربي لاختبار تشكيل الحروف.",
        "यह एक छोटा अनुच्छेद है जिसमें संयुक्ताक्षरों का परीक्षण है।",
        "Latin ثم العربية ثم Latin. देवनागरी and office.",
    };
    for (unsigned i = 0; i < 4; ++i) compare(&cached, &reference, paragraphs[i], 0, NULL, 0);
    fontshaper_un used = cached.context->CacheMemory.Used;
    for (unsigned repeat = 0; repeat < 10; ++repeat) {
        for (unsigned i = 0; i < 4; ++i) compare(&cached, &reference, paragraphs[i], 0, NULL, 0);
        CHECK(cached.context->CacheMemory.Used == used);
    }
    destroy_context(&reference);
    destroy_context(&cached);
}

int main(int argc, char** argv) {
    const char* paths[] = {
        "/usr/share/fonts/TTF/NotoSerifNerdFont-Regular.ttf",
        "/usr/share/fonts/noto/NotoSansArabic-Regular.ttf",
        "/usr/share/fonts/noto/NotoSansDevanagari-Regular.ttf",
    };
    for (int i = 1; i < argc && i <= 3; ++i) paths[i - 1] = argv[i];
    TestFont fonts[3];
    for (unsigned i = 0; i < 3; ++i) fonts[i] = load_font(paths[i]);
    configurations(fonts);
    bounds_and_fonts(fonts);
    multilingual(fonts);
    for (unsigned i = 0; i < 3; ++i) {
        free(fonts[i].compiled);
        free(fonts[i].blob);
    }
    puts("fontshaper context: bounds, persistent configs, feature keys, scratch lifetimes, and multilingual checks passed");
    return 0;
}
