/* From the repository root:
 * clang -O2 -I src tests/fontshaper_context_test.c -lm -o /tmp/fontshaper-shaper-test
 * /tmp/fontshaper-shaper-test [Latin-font [Arabic-font [Devanagari-font]]]
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
// Historical headers/spike are regression oracles; production exposes only state names.
#ifndef FONTSHAPER_STATE_API_VERSION
#ifdef FONTSHAPER_CONTEXT_API_VERSION
#define FONTSHAPER_STATE_API_VERSION FONTSHAPER_CONTEXT_API_VERSION
#endif
#define FontShaperState FontShaperShapeContext
#define FontShaperStateInfo fontshaper_shape_create_info
#define FontShaperStateFlags FontShaperShapeContextFlags
#define font_shaper_size_of_state_header font_shaper_size_of_shape_context
#define font_shaper_place_state_header font_shaper_place_shape_context
#define font_shaper_set_state_cache font_shaper_set_context_cache
#define font_shaper_size_of_state_scratch font_shaper_size_of_context_scratch
#endif
#ifndef FONTSHAPER_STATE_API_VERSION
#define font_shaper_shape_begin_with_memory font_shaper_shape_begin
#endif

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define INPUT_CAPACITY 512
#define GLYPH_CAPACITY 1024
#define CACHE_BYTES (8 * 1024 * 1024)

typedef struct TestFont {
    FontShaperFont font;
    void* blob;
    void* compiled;
} TestFont;

typedef struct TestState {
    FontShaperState* shaper;
    void* memory;
    void* destination;
    void* scratch[2];
    void* cache;
    FontShaperUn scratch_size;
    FontShaperUn input_capacity, glyph_capacity;
    unsigned pass;
} TestState;

typedef struct Glyph {
    FontShaperFont* font;
    int id, codepoint, source, user_id, advance_x, advance_y, offset_x, offset_y;
    int direction, paragraph_direction;
} Glyph;

typedef struct Result {
    unsigned count;
    Glyph glyphs[GLYPH_CAPACITY];
} Result;

static void* allocate(FontShaperUn bytes) {
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
    void* bytes = allocate((FontShaperUn)length);
    CHECK(fread(bytes, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    TestFont result = {0};
    FontShaperLoadFontState state = {0};
    int scratch_size = 0, blob_size = 0;
    CHECK(font_shaper_load_font(&result.font, &state, bytes, (int)length, 0, &scratch_size, &blob_size) ==
          FONTSHAPER_LOAD_FONT_ERROR_NEED_TO_CREATE_BLOB);
    void* scratch = allocate((FontShaperUn)scratch_size);
    result.blob = allocate((FontShaperUn)blob_size);
    CHECK(font_shaper_place_blob(&result.font, &state, scratch, result.blob) == FONTSHAPER_LOAD_FONT_ERROR_NONE);
    free(scratch);
    free(bytes);
    result.compiled = allocate(font_shaper_size_of_compiled_font(&result.font));
    scratch = allocate(font_shaper_size_of_font_compile_scratch(&result.font));
    CHECK(font_shaper_compile_font(&result.font, result.compiled, scratch, NULL) == FONTSHAPER_LOAD_FONT_ERROR_NONE);
    free(scratch);
    return result;
}

static void resize_scratch(TestState* state, FontShaperLanguage language) {
    FontShaperUn size = font_shaper_size_of_state_scratch(state->shaper, language,
                                                        state->input_capacity, state->glyph_capacity);
    CHECK(size);
    if (size == state->scratch_size) return;
    state->scratch_size = size;
    for (unsigned i = 0; i < 2; ++i) {
        free(state->scratch[i]);
        state->scratch[i] = allocate(size);
    }
}

static TestState make_test_state(TestFont* fonts, unsigned font_count, FontShaperUn cache_size) {
    TestState result = {0};
    result.input_capacity = INPUT_CAPACITY;
    result.glyph_capacity = GLYPH_CAPACITY;
    result.memory = allocate((FontShaperUn)font_shaper_size_of_state_header());
    result.shaper = font_shaper_place_state_header(result.memory, 0);
    CHECK(result.shaper);
    for (unsigned i = 0; i < font_count; ++i)
        CHECK(font_shaper_shape_push_font(result.shaper, &fonts[i].font) == &fonts[i].font);
    result.destination = allocate(font_shaper_size_of_glyph_storage(GLYPH_CAPACITY));
    if (cache_size) {
        result.cache = allocate(cache_size);
        CHECK(font_shaper_set_state_cache(result.shaper, result.cache, cache_size));
    }
    resize_scratch(&result, FONTSHAPER_LANGUAGE_DONT_KNOW);
    return result;
}

static void free_test_state(TestState* shaper) {
    free(shaper->cache);
    free(shaper->scratch[1]);
    free(shaper->scratch[0]);
    free(shaper->destination);
    free(shaper->memory);
}

static Result finish(TestState* state) {
    FontShaperState* shaper = state->shaper;
    font_shaper_shape_end(shaper);
    CHECK(font_shaper_shape_error(shaper) == FONTSHAPER_SHAPE_ERROR_NONE);
    Result result = {0};
    FontShaperRun run;
    while (font_shaper_shape_run(shaper, &run)) {
        FontShaperGlyph* glyph;
        while (font_shaper_glyph_iterator_next(&run.glyphs, &glyph)) {
            CHECK(result.count < GLYPH_CAPACITY);
            FontShaperShapeCodepoint input;
            CHECK(font_shaper_shape_get_shape_codepoint(shaper, glyph->user_id_or_codepoint_index, &input));
            result.glyphs[result.count++] = (Glyph){
                .font = run.font, .id = glyph->id, .codepoint = (int)glyph->codepoint,
                .source = glyph->user_id_or_codepoint_index, .user_id = input.user_id,
                .advance_x = glyph->advance_x, .advance_y = glyph->advance_y,
                .offset_x = glyph->offset_x, .offset_y = glyph->offset_y,
                .direction = run.direction, .paragraph_direction = run.paragraph_direction,
            };
        }
    }
    CHECK(font_shaper_shape_error(shaper) == FONTSHAPER_SHAPE_ERROR_NONE);
    /* Destroy the just-used transient data and alternate backing buffers. Cached
     * keys/configs must not point at either paragraph scratch allocation. */
    CHECK(shaper->pass_memory.capacity == state->scratch_size);
    CHECK(shaper->pass_memory.used <= state->scratch_size);
    memset(shaper->pass_memory.data, 0xa5, (size_t)shaper->pass_memory.used);
    return result;
}

static Result shape(TestState* state, const char* text, FontShaperLanguage language,
                    FontShaperFeatureOverride* features, unsigned feature_count) {
    FontShaperState* shaper = state->shaper;
    for (unsigned i = 0; i < feature_count; ++i)
        font_shaper_shape_push_feature(shaper, features[i].tag, features[i].value);
    resize_scratch(state, language);
    font_shaper_shape_begin_with_memory(shaper, FONTSHAPER_DIRECTION_DONT_KNOW, language,
                         state->destination, state->scratch[state->pass++ & 1], state->input_capacity, state->glyph_capacity);
    font_shaper_shape_utf8(shaper, text, (int)strlen(text), FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    Result result = finish(state);
    for (unsigned i = feature_count; i > 0; --i)
        CHECK(font_shaper_shape_pop_feature(shaper, features[i - 1].tag));
    return result;
}

static Result mixed_styles(TestState* state) {
    FontShaperState* shaper = state->shaper;
    font_shaper_shape_begin_with_memory(shaper, FONTSHAPER_DIRECTION_LTR, 0, state->destination,
                         state->scratch[state->pass++ & 1], INPUT_CAPACITY, GLYPH_CAPACITY);
    for (unsigned span = 0; span < 4; ++span) {
        font_shaper_shape_push_feature(shaper, FONTSHAPER_FEATURE_TAG_liga, (int)(span & 1));
        font_shaper_shape_utf8(shaper, "office fi AV ", 13, FONTSHAPER_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);
        CHECK(font_shaper_shape_pop_feature(shaper, FONTSHAPER_FEATURE_TAG_liga));
        font_shaper_shape_manual_break(shaper);
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

static void compare(TestState* cached, TestState* reference, const char* text,
                    FontShaperLanguage language, FontShaperFeatureOverride* features, unsigned count) {
    Result actual = shape(cached, text, language, features, count);
    Result expected = shape(reference, text, language, features, count);
    same_result(&actual, &expected);
}

static void configurations(TestFont* fonts) {
    TestState cached = make_test_state(fonts, 1, CACHE_BYTES);
    TestState reference = make_test_state(fonts, 1, 0);
    FontShaperFeatureOverride off[] = {{FONTSHAPER_FEATURE_TAG_liga, 0}, {FONTSHAPER_FEATURE_TAG_kern, 1}};
    FontShaperFeatureOverride reordered[] = {{FONTSHAPER_FEATURE_TAG_kern, 1}, {FONTSHAPER_FEATURE_TAG_liga, 0}};
    FontShaperFeatureOverride on[] = {{FONTSHAPER_FEATURE_TAG_liga, 1}, {FONTSHAPER_FEATURE_TAG_kern, 0}};
    FontShaperFeatureOverride last_wins[] = {{FONTSHAPER_FEATURE_TAG_liga, 1},
        {FONTSHAPER_FEATURE_TAG_kern, 1}, {FONTSHAPER_FEATURE_TAG_liga, 0}};
    FontShaperFeatureOverride nonbinary[] = {{FONTSHAPER_FEATURE_TAG_salt, 2}};
    const char* text = "The efficient office staff finished fifty files. AV To fi ffi.";
    compare(&cached, &reference, text, 0, off, 2);
    FontShaperUn used = cached.shaper->cache_memory.used;
    FontShaperShapeConfig* shape_config = cached.shaper->prepared_shape_config;
    FontShaperGlyphConfig* glyph_config = cached.shaper->prepared_glyph_config;
    CHECK(shape_config && glyph_config);
    compare(&cached, &reference, text, 0, reordered, 2);
    CHECK(cached.shaper->cache_memory.used == used);
    CHECK(cached.shaper->prepared_shape_config == shape_config);
    CHECK(cached.shaper->prepared_glyph_config == glyph_config);
    compare(&cached, &reference, text, 0, last_wins, 3);
    CHECK(cached.shaper->cache_memory.used == used);
    compare(&cached, &reference, text, 0, on, 2);
    CHECK(cached.shaper->prepared_glyph_config != glyph_config);
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
        FontShaperFeatureOverride value = {FONTSHAPER_FEATURE_TAG_salt, (int)i + 10};
        compare(&cached, &reference, text, 0, &value, 1);
    }
    used = cached.shaper->cache_memory.used;
    for (unsigned i = 0; i < 40; ++i) {
        FontShaperFeatureOverride value = {FONTSHAPER_FEATURE_TAG_salt, (int)i + 10};
        compare(&cached, &reference, text, 0, &value, 1);
        CHECK(cached.shaper->cache_memory.used == used);
    }
    cached.input_capacity = 128;
    cached.glyph_capacity = 256;
    compare(&cached, &reference, text, 0, reordered, 2);
    CHECK(cached.shaper->cached_codepoint_capacity == 128 && cached.shaper->cached_glyph_capacity == 256);
    CHECK(cached.shaper->cache_memory.used == used);
    cached.input_capacity = INPUT_CAPACITY;
    cached.glyph_capacity = GLYPH_CAPACITY;
    for (unsigned i = 0; i < 100; ++i) {
        compare(&cached, &reference, text, 0, reordered, 2);
        CHECK(cached.shaper->cache_memory.used == used);
    }
    FontShaperUn small = cached.scratch_size;
    CHECK(font_shaper_set_state_cache(cached.shaper, NULL, 0));
    CHECK(!cached.shaper->cached_scratch_size);
    compare(&cached, &reference, text, 0, off, 2);
    CHECK(cached.scratch_size == reference.scratch_size && cached.scratch_size > small);
    CHECK(font_shaper_set_state_cache(cached.shaper, cached.cache, CACHE_BYTES));
    CHECK(!cached.shaper->cached_scratch_size);
    compare(&cached, &reference, text, 0, off, 2);
    CHECK(cached.scratch_size == small);
    CHECK(!font_shaper_set_state_cache(cached.shaper, NULL, 1));
    CHECK(!font_shaper_set_state_cache(cached.shaper, cached.cache, 0));
    CHECK(font_shaper_set_state_cache(cached.shaper, (char*)cached.cache + 1, CACHE_BYTES - 1));
    CHECK(!cached.shaper->cached_scratch_size);
    compare(&cached, &reference, text, 0, on, 2);
    CHECK(cached.scratch_size == small);
    free_test_state(&reference);
    free_test_state(&cached);
}

static void bounds_and_fonts(TestFont* fonts) {
    TestState state = make_test_state(fonts, 1, CACHE_BYTES);
    FontShaperState* shaper = state.shaper;
    FontShaperUn original = shaper->cached_scratch_size;
    CHECK(original == state.scratch_size);
    CHECK(font_shaper_size_of_state_scratch(shaper, 0, INPUT_CAPACITY, GLYPH_CAPACITY) == original);
    CHECK(font_shaper_size_of_state_scratch(shaper, 0, INPUT_CAPACITY + 1, GLYPH_CAPACITY));
    CHECK(shaper->cached_codepoint_capacity == INPUT_CAPACITY + 1);
    CHECK(font_shaper_size_of_state_scratch(shaper, 0, INPUT_CAPACITY, GLYPH_CAPACITY + 1));
    CHECK(shaper->cached_glyph_capacity == GLYPH_CAPACITY + 1);
    FontShaperLanguage language = FONTSHAPER_FOURCC('T', 'R', 'K', ' ');
    CHECK(font_shaper_size_of_state_scratch(shaper, language, INPUT_CAPACITY, GLYPH_CAPACITY));
    CHECK(shaper->cached_scratch_language == language);
    CHECK(!font_shaper_size_of_state_scratch(shaper, 0, (FontShaperUn)1 << 27, GLYPH_CAPACITY));
    CHECK(!font_shaper_size_of_state_scratch(shaper, 0, INPUT_CAPACITY, (FontShaperUn)-1));
    Result before = shape(&state, "office AV", 0, NULL, 0);
    CHECK(shaper->cache_memory.used);
    CHECK(font_shaper_shape_push_font(shaper, &fonts[0].font));
    CHECK(!shaper->cached_scratch_size && shaper->config_cache_dirty);
    CHECK(font_shaper_shape_pop_font(shaper) == &fonts[0].font);
    CHECK(!shaper->cached_scratch_size);
    Result after = shape(&state, "office AV", 0, NULL, 0);
    same_result(&before, &after);
    CHECK(!shaper->config_cache_dirty);
    CHECK(font_shaper_shape_push_font(shaper, &fonts[1].font));
    Result arabic = shape(&state, "مرحبا بالعالم", 0, NULL, 0);
    CHECK(arabic.count);
    for (unsigned i = 0; i < arabic.count; ++i) CHECK(arabic.glyphs[i].font == &fonts[1].font);
    CHECK(font_shaper_shape_pop_font(shaper) == &fonts[1].font);
    after = shape(&state, "office AV", 0, NULL, 0);
    same_result(&before, &after);
    FontShaperFont invalid = {0};
    CHECK(font_shaper_shape_push_font(shaper, &invalid) == &invalid);
    CHECK(!font_shaper_size_of_state_scratch(shaper, 0, INPUT_CAPACITY, GLYPH_CAPACITY));
    CHECK(font_shaper_shape_pop_font(shaper) == &invalid);
    CHECK(font_shaper_size_of_state_scratch(shaper, 0, INPUT_CAPACITY, GLYPH_CAPACITY) == original);
    FontShaperUn exact_size = shaper->cache_memory.used;
    void* exact = allocate(exact_size);
    CHECK(font_shaper_set_state_cache(shaper, exact, exact_size));
    after = shape(&state, "office AV", 0, NULL, 0);
    same_result(&before, &after);
    CHECK(shaper->cache_memory.used == exact_size);
    font_shaper_shape_push_feature(shaper, FONTSHAPER_FEATURE_TAG_liga, 0);
    font_shaper_shape_begin_with_memory(shaper, FONTSHAPER_DIRECTION_LTR, 0, state.destination,
                         state.scratch[0], INPUT_CAPACITY, GLYPH_CAPACITY);
    font_shaper_shape_utf8(shaper, "office", 6, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    font_shaper_shape_end(shaper);
    CHECK(font_shaper_shape_error(shaper) == FONTSHAPER_SHAPE_ERROR_OUT_OF_MEMORY);
    CHECK(shaper->cache_memory.used <= exact_size);
    free(exact);
    free_test_state(&state);

    TestState tiny = make_test_state(fonts, 1, 1);
    font_shaper_shape_begin_with_memory(tiny.shaper, FONTSHAPER_DIRECTION_LTR, 0, tiny.destination,
                         tiny.scratch[0], INPUT_CAPACITY, GLYPH_CAPACITY);
    font_shaper_shape_utf8(tiny.shaper, "office", 6, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    font_shaper_shape_end(tiny.shaper);
    CHECK(font_shaper_shape_error(tiny.shaper) == FONTSHAPER_SHAPE_ERROR_OUT_OF_MEMORY);
    FontShaperRun run;
    CHECK(!font_shaper_shape_run(tiny.shaper, &run));
    CHECK(!font_shaper_set_state_cache(tiny.shaper, NULL, 0));
    free_test_state(&tiny);

    TestState limited = make_test_state(fonts, 1, CACHE_BYTES);
    font_shaper_shape_begin_with_memory(limited.shaper, FONTSHAPER_DIRECTION_LTR, 0, limited.destination,
                         limited.scratch[0], INPUT_CAPACITY, 1);
    font_shaper_shape_utf8(limited.shaper, "AV", 2, FONTSHAPER_USER_ID_GENERATION_MODE_SOURCE_INDEX);
    font_shaper_shape_end(limited.shaper);
    while (font_shaper_shape_run(limited.shaper, &run)) {}
    CHECK(font_shaper_shape_error(limited.shaper) == FONTSHAPER_SHAPE_ERROR_OUT_OF_MEMORY);
    free_test_state(&limited);
}

static void multilingual(TestFont* fonts) {
    TestState cached = make_test_state(fonts, 3, CACHE_BYTES);
    TestState reference = make_test_state(fonts, 3, 0);
    const char* paragraphs[] = {
        "Latin, Ελληνικά, русский текст. Cafe\xcc\x81 and office.",
        "هذا نص عربي لاختبار تشكيل الحروف.",
        "यह एक छोटा अनुच्छेद है जिसमें संयुक्ताक्षरों का परीक्षण है।",
        "Latin ثم العربية ثم Latin. देवनागरी and office.",
    };
    for (unsigned i = 0; i < 4; ++i) compare(&cached, &reference, paragraphs[i], 0, NULL, 0);
    FontShaperUn used = cached.shaper->cache_memory.used;
    for (unsigned repeat = 0; repeat < 10; ++repeat) {
        for (unsigned i = 0; i < 4; ++i) compare(&cached, &reference, paragraphs[i], 0, NULL, 0);
        CHECK(cached.shaper->cache_memory.used == used);
    }
    free_test_state(&reference);
    free_test_state(&cached);
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
    puts("fontshaper state: bounds, persistent configs, feature keys, scratch lifetimes, and multilingual checks passed");
    return 0;
}
