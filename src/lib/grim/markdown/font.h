#pragma once

#include <lib/grim/bp.h>
#include <lib/grim/assert.h>
#include <lib/grim/logger.h>
#include <lib/grim/math.h>
#include <lib/grim/mem/arena.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// Each including translation unit owns its static Fontshaper implementation.
#define FONTSHAPER_STATIC
#define FONTSHAPER_IMPLEMENTATION
#define FONTSHAPER_ASSERT(condition) assert(SCOPE_STARTUP, (condition))
// Xlib defines this common identifier as a macro; do not leak it into the vendor.
#pragma push_macro("Unsorted")
#undef Unsorted
#include <lib/fontshaper/fontshaper.h>
#pragma pop_macro("Unsorted")
#undef FONTSHAPER_IMPLEMENTATION

typedef struct { const u8 *d; u32 len, pos; } TTFile;

internal void tt_require(bool condition, const char *message) {
    if (!condition) {
        CRITICAL_LOG(SCOPE_STARTUP, "Invalid or unsupported TrueType font: %s", message);
        exit(1);
    }
}
internal void tt_need(const TTFile *f, u32 n) {
    tt_require(f->pos <= f->len && n <= f->len - f->pos, "truncated data");
}
internal u32 tt_r32(TTFile *f) {
    tt_need(f, 4); const u8 *p = f->d + f->pos; f->pos += 4;
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
internal u16 tt_r16(TTFile *f) {
    tt_need(f, 2); const u8 *p = f->d + f->pos; f->pos += 2;
    return ((u16)p[0] << 8) | p[1];
}
internal i16 tt_rs16(TTFile *f) { return (i16)tt_r16(f); }
internal u8 tt_r8(TTFile *f) { tt_need(f, 1); return f->d[f->pos++]; }
internal void tt_skip(TTFile *f, u32 n) { tt_need(f, n); f->pos += n; }
internal TTFile tt_slice(TTFile f, u32 off, u32 len) {
    tt_require(off <= f.len && len <= f.len - off, "table bounds");
    return (TTFile){f.d + off, len, 0};
}

typedef struct {
    const u8 *data;
    u32 data_len, glyph_count;
    u16 upem, num_hmetrics;
    i16 loca_fmt;
    f32 scale, ascent, descent, line_gap, line_height;
    TTFile loca, glyf, hmtx;
} TTFont;

typedef struct { f32 x, y; u8 on; } TTPoint;
typedef struct { TTPoint *pts; u32 n; } TTContour;
typedef struct { TTContour *cs; u32 nc; } TTGlyph;
typedef struct { f32 advance_width; } GlyphMetrics;
typedef struct {
    TTFont *font;
    GlyphMetrics *metrics;
    u32 glyph_count;
    fontshaper_font *shaper; // Stable address: compiled configs borrow this object.
    void *blob_memory, *compiled_memory;
    u64 identity;
} FontState;

internal TTFile tt_table(TTFile file, u32 tag) {
    TTFile directory = file;
    tt_require(tt_r32(&directory) == 0x00010000, "expected TrueType quadratic outlines");
    u16 count = tt_r16(&directory);
    tt_skip(&directory, 6);
    tt_need(&directory, (u32)count * 16);
    for (u32 i = 0; i < count; i++) {
        u32 candidate = tt_r32(&directory);
        tt_skip(&directory, 4);
        u32 offset = tt_r32(&directory), length = tt_r32(&directory);
        if (candidate == tag) return tt_slice(file, offset, length);
    }
    tt_require(false, "required table missing");
    return (TTFile){0};
}

internal TTFont *tt_open(Arena *a, const char *path, f32 size, bool points) {
    i32 fd = open(path, O_RDONLY | O_CLOEXEC);
    tt_require(fd >= 0, "cannot open file");
    struct stat st;
    tt_require(fstat(fd, &st) == 0 && st.st_size > 0 && (u64)st.st_size <= UINT32_MAX,
               "invalid file length");
    const u8 *data = mmap(NULL, (usize)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    tt_require(data != MAP_FAILED, "cannot map file");
    TTFont *f = arena_alloc_aligned(a, TTFont, 1);
    *f = (TTFont){.data = data, .data_len = (u32)st.st_size};
    TTFile file = {data, f->data_len, 0};
    TTFile head = tt_table(file, 0x68656164), maxp = tt_table(file, 0x6D617870);
    TTFile hhea = tt_table(file, 0x68686561);
    tt_skip(&head, 18); f->upem = tt_r16(&head);
    tt_skip(&head, 30); f->loca_fmt = tt_rs16(&head);
    tt_require(f->upem >= 16 && f->upem <= 16384 && (f->loca_fmt == 0 || f->loca_fmt == 1),
               "head metrics");
    tt_skip(&maxp, 4); f->glyph_count = tt_r16(&maxp);
    tt_skip(&hhea, 4);
    i16 asc = tt_rs16(&hhea), desc = tt_rs16(&hhea), gap = tt_rs16(&hhea);
    tt_skip(&hhea, 24); f->num_hmetrics = tt_r16(&hhea);
    tt_require(f->glyph_count && f->num_hmetrics && f->num_hmetrics <= f->glyph_count &&
               asc > desc && isfinite(size) && size > 0, "font metrics");
    f->scale = points ? (size * (96.0f / 72.0f)) / f->upem : size / (f32)(asc - desc);
    f->ascent = asc * f->scale; f->descent = desc * f->scale;
    f->line_gap = gap * f->scale; f->line_height = f->ascent - f->descent + f->line_gap;
    tt_require(isfinite(f->line_height) && f->line_height > 0, "line height");
    f->loca = tt_table(file, 0x6C6F6361); f->glyf = tt_table(file, 0x676C7966);
    f->hmtx = tt_table(file, 0x686D7478);
    tt_need(&f->loca, (f->glyph_count + 1) * (f->loca_fmt ? 4 : 2));
    tt_need(&f->hmtx, f->num_hmetrics * 4 + (f->glyph_count - f->num_hmetrics) * 2);
    return f;
}


internal u32 tt_glyph_offset(const TTFont *f, u32 gi) {
    tt_require(gi <= f->glyph_count, "loca glyph index");
    TTFile loca = f->loca; loca.pos = gi * (f->loca_fmt ? 4 : 2);
    u32 offset = f->loca_fmt ? tt_r32(&loca) : (u32)tt_r16(&loca) * 2;
    tt_require(offset <= f->glyf.len, "loca offset");
    return offset;
}

internal TTGlyph tt_load_glyph_raw(Arena *a, const TTFont *f, u32 gi, u32 depth) {
    tt_require(gi < f->glyph_count && depth <= 16, "compound cycle or glyph index");
    u32 off = tt_glyph_offset(f, gi), end = tt_glyph_offset(f, gi + 1);
    tt_require(end >= off, "loca order");
    TTGlyph g = {0};
    if (off == end) return g;
    TTFile file = tt_slice(f->glyf, off, end - off);
    i16 nc = tt_rs16(&file); tt_skip(&file, 8);
    if (nc > 0) {
        u16 *ends = arena_alloc_aligned(a, u16, (u32)nc);
        for (u32 i = 0; i < (u32)nc; i++) {
            ends[i] = tt_r16(&file);
            tt_require(!i || ends[i] > ends[i - 1], "contour endpoint order");
        }
        u32 np = (u32)ends[nc - 1] + 1;
        u16 instructions = tt_r16(&file); tt_skip(&file, instructions);
        u8 *flags = arena_alloc_aligned(a, u8, np);
        for (u32 i = 0; i < np;) {
            u8 flag = tt_r8(&file); u32 repeat = (flag & 8) ? tt_r8(&file) : 0;
            tt_require(repeat < np - i, "point flag repeat");
            do { flags[i++] = flag; } while (repeat--);
        }
        TTPoint *pts = arena_alloc_aligned(a, TTPoint, np);
        for (u32 axis = 0; axis < 2; axis++) {
            i32 previous = 0;
            u8 short_bit = axis ? 4 : 2, same_bit = axis ? 32 : 16;
            for (u32 i = 0; i < np; i++) {
                i32 delta = 0;
                if (flags[i] & short_bit) delta = (i32)tt_r8(&file) * ((flags[i] & same_bit) ? 1 : -1);
                else if (!(flags[i] & same_bit)) delta = tt_rs16(&file);
                previous += delta;
                tt_require(previous >= INT16_MIN && previous <= INT16_MAX, "point coordinate overflow");
                if (axis) pts[i].y = (f32)previous; else pts[i].x = (f32)previous;
                pts[i].on = flags[i] & 1;
            }
        }
        // Slug inserts implicit quadratic midpoints while emitting curves, so
        // raw points need no expanded contour copy here.
        g.nc = (u32)nc; g.cs = arena_alloc_aligned(a, TTContour, g.nc);
        u32 start = 0;
        for (u32 i = 0; i < g.nc; i++) {
            g.cs[i] = (TTContour){pts + start, (u32)ends[i] + 1 - start};
            start = (u32)ends[i] + 1;
        }
    } else if (nc == -1) {
        u32 cap = 8; g.cs = arena_alloc_aligned(a, TTContour, cap);
        u16 flags;
        do {
            flags = tt_r16(&file); u32 child = tt_r16(&file);
            i32 arg1, arg2;
            if (flags & 1) {
                arg1 = flags & 2 ? tt_rs16(&file) : tt_r16(&file);
                arg2 = flags & 2 ? tt_rs16(&file) : tt_r16(&file);
            } else {
                arg1 = flags & 2 ? (i8)tt_r8(&file) : tt_r8(&file);
                arg2 = flags & 2 ? (i8)tt_r8(&file) : tt_r8(&file);
            }
            f32 dx = flags & 2 ? arg1 : 0, dy = flags & 2 ? arg2 : 0;
            f32 xx = 1, xy = 0, yx = 0, yy = 1;
            u32 transforms = !!(flags & 8) + !!(flags & 64) + !!(flags & 128);
            tt_require(transforms <= 1 && (flags & 0x1800) != 0x1800, "compound transform flags");
            if (flags & 128) {
                xx = tt_rs16(&file) / 16384.0f; xy = tt_rs16(&file) / 16384.0f;
                yx = tt_rs16(&file) / 16384.0f; yy = tt_rs16(&file) / 16384.0f;
            } else if (flags & 64) {
                xx = tt_rs16(&file) / 16384.0f; yy = tt_rs16(&file) / 16384.0f;
            } else if (flags & 8) { xx = yy = tt_rs16(&file) / 16384.0f; }
            if ((flags & 2) && (flags & 0x0800)) {
                f32 x = dx; dx = xx * x + yx * dy; dy = xy * x + yy * dy;
            }
            TTGlyph sub = tt_load_glyph_raw(a, f, child, depth + 1);
            if (!(flags & 2)) {
                // Component point numbers refer to original contour points,
                // not the implicit midpoints inserted later by Slug.
                TTPoint *parent = NULL, *component = NULL;
                u32 at = (u32)arg1;
                for (u32 ci = 0; ci < g.nc; ci++) {
                    if (at < g.cs[ci].n) { parent = &g.cs[ci].pts[at]; break; }
                    at -= g.cs[ci].n;
                }
                at = (u32)arg2;
                for (u32 ci = 0; ci < sub.nc; ci++) {
                    if (at < sub.cs[ci].n) { component = &sub.cs[ci].pts[at]; break; }
                    at -= sub.cs[ci].n;
                }
                tt_require(parent && component, "compound attachment point index");
                dx = parent->x - (xx * component->x + yx * component->y);
                dy = parent->y - (xy * component->x + yy * component->y);
            } else if (flags & 4) {
                dx = roundf(dx); dy = roundf(dy);
            }
            tt_require(sub.nc <= 65535 - g.nc, "compound contour count");
            if (g.nc + sub.nc > cap) {
                while (cap < g.nc + sub.nc) cap *= 2;
                TTContour *old = g.cs; g.cs = arena_alloc_aligned(a, TTContour, cap);
                memory_copy(g.cs, old, g.nc * sizeof(*old));
            }
            for (u32 ci = 0; ci < sub.nc; ci++) {
                for (u32 pi = 0; pi < sub.cs[ci].n; pi++) {
                    TTPoint *p = &sub.cs[ci].pts[pi]; f32 x = p->x, y = p->y;
                    p->x = xx * x + yx * y + dx; p->y = xy * x + yy * y + dy;
                }
                g.cs[g.nc++] = sub.cs[ci];
            }
        } while (flags & 32);
        if (flags & 256) { u16 instructions = tt_r16(&file); tt_skip(&file, instructions); }
    } else tt_require(nc == 0, "contour count");
    return g;
}

internal TTGlyph tt_get_glyph(Arena *a, const TTFont *f, u32 gi) {
    TTGlyph g = tt_load_glyph_raw(a, f, gi, 0);
    for (u32 ci = 0; ci < g.nc; ci++) for (u32 pi = 0; pi < g.cs[ci].n; pi++) {
        g.cs[ci].pts[pi].x *= f->scale; g.cs[ci].pts[pi].y *= f->scale;
    }
    return g;
}

internal FontState font_load(Arena *a, const char *path, f32 size, bool points) {
    static u64 next_identity;
    FontState state = {.font = tt_open(a, path, size, points), .identity = ++next_identity};
    TTFont *f = state.font;
    state.glyph_count = f->glyph_count;
    state.metrics = arena_alloc_aligned(a, GlyphMetrics, state.glyph_count);
    for (u32 i = 0; i < state.glyph_count; i++) {
        TTFile metrics = f->hmtx;
        metrics.pos = min(i, (u32)f->num_hmetrics - 1) * 4;
        state.metrics[i].advance_width = tt_r16(&metrics) * f->scale;
    }
    tt_require(f->data_len <= INT_MAX, "Fontshaper source exceeds API length limit");
    state.shaper = calloc(1, sizeof(*state.shaper));
    tt_require(state.shaper != NULL, "Fontshaper font allocation");
    fontshaper_load_font_state load = {0};
    int scratch_bytes = 0, blob_bytes = 0;
    fontshaper_load_font_error error = fontshaper_LoadFont(state.shaper, &load,
        (void *)f->data, (int)f->data_len, 0, &scratch_bytes, &blob_bytes);
    // tt_open accepts sfnt TrueType input, never an already-native blob.
    tt_require(error == FONTSHAPER_LOAD_FONT_ERROR_NEED_TO_CREATE_BLOB &&
        scratch_bytes > 0 && blob_bytes > 0, "Fontshaper native blob size query");
    state.blob_memory = malloc((usize)blob_bytes);
    void *scratch = malloc((usize)scratch_bytes);
    tt_require(state.blob_memory && scratch, "Fontshaper native blob allocation");
    error = fontshaper_PlaceBlob(state.shaper, &load, scratch, state.blob_memory);
    free(scratch);
    tt_require(error == FONTSHAPER_LOAD_FONT_ERROR_NONE, "Fontshaper native blob conversion");
    fontshaper_un compiled_bytes = fontshaper_SizeOfCompiledFont(state.shaper);
    fontshaper_un compile_scratch_bytes = fontshaper_SizeOfFontCompileScratch(state.shaper);
    tt_require(compiled_bytes && compiled_bytes <= SIZE_MAX &&
        compile_scratch_bytes && compile_scratch_bytes <= SIZE_MAX, "Fontshaper compile size query");
    state.compiled_memory = malloc((usize)compiled_bytes);
    scratch = malloc((usize)compile_scratch_bytes);
    tt_require(state.compiled_memory && scratch, "Fontshaper compile allocation");
    error = fontshaper_CompileFont(state.shaper, state.compiled_memory, scratch, NULL);
    free(scratch);
    tt_require(error == FONTSHAPER_LOAD_FONT_ERROR_NONE && fontshaper_FontIsValid(state.shaper),
        "Fontshaper font compilation");
    return state;
}

// Outlines are startup-only. The independent native shaping blob stays alive.
internal void tt_release_data(TTFont *f) {
    if (f->data) munmap((void *)f->data, f->data_len);
    f->data = NULL; f->data_len = 0;
    f->loca = f->glyf = f->hmtx = (TTFile){0};
}
internal void font_destroy(FontState *state) {
    if (state->font) tt_release_data(state->font);
    free(state->compiled_memory);
    free(state->blob_memory);
    free(state->shaper);
    *state = (FontState){0};
}

internal u32 font_glyph(const FontState *font, i32 cp) {
    if (!font || !font->shaper || cp < 0 || cp > 0x10ffff) return 0;
    int glyph = fontshaper_CodepointToGlyphId(font->shaper, cp);
    return glyph >= 0 && (u32)glyph < font->glyph_count ? (u32)glyph : 0;
}

typedef struct {
    u32 glyph, source; // Raw glyph ID and original input codepoint index.
    f32 x, y, advance; // Pixel origin (y up) and horizontal advance.
    f32 pen_begin, pen_end; // Advance interval, independent of ink/mark offsets.
    bool rtl;
} FontShapeGlyph;

typedef struct FontShapeConfig {
    struct FontShapeConfig *next;
    fontshaper_script script;
    void *config_memory, *feature_memory, *scratch_memory;
    fontshaper_shape_config *config;
    fontshaper_glyph_config *without_ligatures;
    fontshaper_shape_scratchpad *scratch;
    u32 capacity;
} FontShapeConfig;

typedef struct {
    u32 first, end, glyph_first, glyph_end;
    fontshaper_script script;
    fontshaper_direction direction, paragraph;
    f32 width;
    bool paragraph_start;
} FontShapeRun;

// Zero-initialize once; reuse across lines/frames. No output pointer survives
// another font_shape call. Destroy before releasing its font's arena.
typedef struct {
    FontShapeGlyph *glyphs;
    u32 count;
    f32 width;
    u32 glyph_capacity, input_capacity, run_capacity, storage_capacity;
    fontshaper_break *breaks;
    FontShapeRun *runs;
    void *storage_memory;
    fontshaper_glyph_storage storage;
    FontShapeConfig *configs;
    fontshaper_font *font;
    u64 font_identity;
    const char *error; // Static diagnostic, NULL after a successful shape.
} FontShape;

internal void font_shape_config_destroy(FontShapeConfig *config) {
    free(config->scratch_memory);
    free(config->feature_memory);
    free(config->config_memory);
    free(config);
}

internal void font_shape_destroy(FontShape *shape) {
    while (shape->configs) {
        FontShapeConfig *next = shape->configs->next;
        font_shape_config_destroy(shape->configs);
        shape->configs = next;
    }
    free(shape->storage_memory);
    free(shape->glyphs);
    free(shape->breaks);
    free(shape->runs);
    *shape = (FontShape){0};
}

internal void *font_shape_reserve(void *memory, u32 *capacity, u32 count, usize stride) {
    if (count <= *capacity) return memory;
    u32 cap = *capacity ? *capacity : 256;
    while (cap < count) cap = cap > UINT32_MAX / 2 ? count : cap * 2;
    if (stride > SIZE_MAX / cap) return NULL;
    void *grown = realloc(memory, (usize)cap * stride);
    if (grown) *capacity = cap;
    return grown;
}

internal FontShapeConfig *font_shape_config(FontShape *shape, fontshaper_script script) {
    for (FontShapeConfig *c = shape->configs; c; c = c->next)
        if (c->script == script) return c;
    FontShapeConfig *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->script = script;
    int bytes = fontshaper_SizeOfShapeConfig(shape->font, script, FONTSHAPER_LANGUAGE_DONT_KNOW);
    fontshaper_un scratch_bytes = fontshaper_SizeOfShapeConfigScratch(
        shape->font, script, FONTSHAPER_LANGUAGE_DONT_KNOW);
    if (bytes <= 0 || !scratch_bytes || scratch_bytes > SIZE_MAX) goto fail;
    c->config_memory = malloc((usize)bytes);
    void *scratch = malloc((usize)scratch_bytes);
    if (!c->config_memory || !scratch) { free(scratch); goto fail; }
    c->config = fontshaper_PlaceShapeConfig(shape->font, script, FONTSHAPER_LANGUAGE_DONT_KNOW,
        c->config_memory, scratch, NULL);
    free(scratch);
    if (!c->config) goto fail;
    c->next = shape->configs;
    shape->configs = c;
    return c;
fail:
    font_shape_config_destroy(c);
    return NULL;
}

internal bool font_shape_workspace(FontShape *shape, FontShapeConfig *c, u32 count, bool ligatures) {
    // A bounded policy, not a guarantee for arbitrary font-driven expansion.
    // Exceeding it is an explicit error; never emit an unshaped replacement.
    if (count > UINT32_MAX / 8) return false;
    u32 needed = max(256u, count * 8);
    if (needed > shape->storage_capacity) {
        u32 cap = shape->storage_capacity ? shape->storage_capacity : 256;
        while (cap < needed) cap = cap > UINT32_MAX / 2 ? needed : cap * 2;
        fontshaper_un bytes = fontshaper_SizeOfGlyphStorage(cap);
        if (!bytes || bytes > SIZE_MAX) return false;
        void *memory = malloc((usize)bytes);
        if (!memory) return false;
        fontshaper_glyph_storage storage;
        if (!fontshaper_InitializeGlyphStorage(&storage, memory, cap)) {
            free(memory); return false;
        }
        free(shape->storage_memory);
        shape->storage_memory = memory;
        shape->storage = storage;
        shape->storage_capacity = cap;
    }
    if (c->capacity < shape->storage_capacity) {
        fontshaper_un bytes = fontshaper_SizeOfShapeScratchpad(c->config, shape->storage_capacity);
        if (!bytes || bytes > SIZE_MAX) return false;
        void *memory = malloc((usize)bytes);
        if (!memory) return false;
        fontshaper_shape_scratchpad *scratch =
            fontshaper_PlaceShapeScratchpad(c->config, memory, shape->storage_capacity);
        if (!scratch) { free(memory); return false; }
        free(c->scratch_memory);
        c->scratch_memory = memory;
        c->scratch = scratch;
        c->capacity = shape->storage_capacity;
    }
    if (!ligatures && !c->without_ligatures) {
        fontshaper_feature_override off[] = {
            {FONTSHAPER_FEATURE_TAG_calt, 0}, {FONTSHAPER_FEATURE_TAG_liga, 0},
            {FONTSHAPER_FEATURE_TAG_clig, 0}};
        int bytes = fontshaper_SizeOfGlyphConfig(c->config, off, 3);
        if (bytes <= 0) return false;
        void *memory = malloc((usize)bytes);
        if (!memory) return false;
        c->without_ligatures = fontshaper_PlaceGlyphConfig(c->config, off, 3, memory);
        if (!c->without_ligatures) { free(memory); return false; }
        c->feature_memory = memory;
    }
    return true;
}

internal bool font_shape(FontShape *shape, const FontState *font,
                         const i32 *codepoints, u32 count, bool ligatures) {
    shape->count = 0; shape->width = 0; shape->error = NULL;
    if (!font || !font->shaper || !font->font || count > INT_MAX || (count && !codepoints)) {
        shape->error = "invalid font or shaping input"; return false;
    }
    if (shape->font != font->shaper || shape->font_identity != font->identity) {
        font_shape_destroy(shape);
        shape->font = font->shaper;
        shape->font_identity = font->identity;
    }
    if (!count) return true;
    fontshaper_break *breaks = font_shape_reserve(shape->breaks, &shape->input_capacity,
        count + 1, sizeof(*breaks));
    if (!breaks) { shape->error = "segmentation buffer allocation"; return false; }
    shape->breaks = breaks;
    memory_zero(breaks, ((usize)count + 1) * sizeof(*breaks));
    FontShapeRun *runs = font_shape_reserve(shape->runs, &shape->run_capacity, count, sizeof(*runs));
    if (!runs) { shape->error = "shaping run allocation"; return false; }
    shape->runs = runs;
    fontshaper_break_state state;
    fontshaper_BreakBegin(&state, FONTSHAPER_DIRECTION_DONT_KNOW,
        FONTSHAPER_JAPANESE_LINE_BREAK_STYLE_NORMAL, 0);
    for (u32 i = 0; i < count; i++) {
        if (codepoints[i] < 0 || codepoints[i] > 0x10ffff ||
            (codepoints[i] >= 0xd800 && codepoints[i] <= 0xdfff)) {
            shape->error = "invalid Unicode scalar"; return false;
        }
        fontshaper_BreakAddCodepoint(&state, codepoints[i], 1, i + 1 == count);
        fontshaper_break b;
        while (fontshaper_Break(&state, &b)) {
            if (b.Position < 0 || (u32)b.Position > count) {
                shape->error = "invalid Fontshaper break position"; return false;
            }
            fontshaper_break *dst = &breaks[b.Position];
            dst->Flags |= b.Flags;
            if (b.Flags & FONTSHAPER_BREAK_FLAG_SCRIPT) dst->Script = b.Script;
            if (b.Flags & FONTSHAPER_BREAK_FLAG_DIRECTION) dst->Direction = b.Direction;
            if (b.Flags & FONTSHAPER_BREAK_FLAG_PARAGRAPH_DIRECTION)
                dst->ParagraphDirection = b.ParagraphDirection;
        }
    }
    u32 run_count = 0;
    fontshaper_script script = FONTSHAPER_SCRIPT_DONT_KNOW;
    fontshaper_direction direction = FONTSHAPER_DIRECTION_DONT_KNOW;
    fontshaper_direction paragraph = FONTSHAPER_DIRECTION_LTR;
    for (u32 i = 0; i < count; i++) {
        fontshaper_break b = breaks[i];
        bool hard = i && (b.Flags & FONTSHAPER_BREAK_FLAG_LINE_HARD);
        if (hard) {
            script = FONTSHAPER_SCRIPT_DONT_KNOW;
            direction = FONTSHAPER_DIRECTION_DONT_KNOW;
            paragraph = FONTSHAPER_DIRECTION_LTR;
        }
        if (b.Flags & FONTSHAPER_BREAK_FLAG_SCRIPT) script = b.Script;
        if ((b.Flags & FONTSHAPER_BREAK_FLAG_PARAGRAPH_DIRECTION) && b.ParagraphDirection)
            paragraph = b.ParagraphDirection;
        if (b.Flags & FONTSHAPER_BREAK_FLAG_DIRECTION) direction = b.Direction;
        fontshaper_direction resolved = direction ? direction : paragraph;
        FontShapeRun *last = run_count ? &runs[run_count - 1] : NULL;
        bool safe = !i || hard || (b.Flags & FONTSHAPER_BREAK_FLAG_GRAPHEME);
        if (!last || (safe && (hard || last->script != script ||
            last->direction != resolved || last->paragraph != paragraph))) {
            if (last) last->end = i;
            runs[run_count++] = (FontShapeRun){.first = i, .end = count, .script = script,
                .direction = resolved, .paragraph = paragraph, .paragraph_start = !i || hard};
        }
    }
    for (u32 r = 0; r < run_count; r++) {
        FontShapeRun *run = &runs[r];
        FontShapeConfig *config = font_shape_config(shape, run->script);
        if (!config) { shape->error = "Fontshaper script config compilation"; goto fail; }
        if (!font_shape_workspace(shape, config, run->end - run->first, ligatures)) {
            shape->error = "Fontshaper workspace allocation or capacity"; goto fail;
        }
        fontshaper_ResetGlyphStorage(&shape->storage);
        for (u32 i = run->first; i < run->end; i++) {
            if (!fontshaper_PushGlyph(&shape->storage, font->shaper, codepoints[i],
                ligatures ? NULL : config->without_ligatures, (int)i)) {
                shape->error = "Fontshaper input glyph capacity"; goto fail;
            }
        }
        fontshaper_glyph_iterator iterator;
        if (fontshaper_ShapeDirect(config->scratch, &shape->storage, run->direction,
            &iterator) != FONTSHAPER_SHAPE_ERROR_NONE) {
            free(config->scratch_memory);
            config->scratch_memory = NULL;
            config->scratch = NULL;
            config->capacity = 0;
            shape->error = "Fontshaper shaping failed (intermediate glyph capacity or invalid font)";
            goto fail;
        }
        run->glyph_first = shape->count;
        f64 x = 0, y = 0;
        fontshaper_glyph *glyph;
        while (fontshaper_GlyphIteratorNext(&iterator, &glyph)) {
            if (glyph->Id >= font->glyph_count || glyph->UserIdOrCodepointIndex < (int)run->first ||
                glyph->UserIdOrCodepointIndex >= (int)run->end || shape->count == UINT32_MAX) {
                shape->error = "invalid Fontshaper output glyph or source"; goto fail;
            }
            FontShapeGlyph *output = font_shape_reserve(shape->glyphs, &shape->glyph_capacity,
                shape->count + 1, sizeof(*output));
            if (!output) { shape->error = "shaped glyph output allocation"; goto fail; }
            shape->glyphs = output;
            output[shape->count++] = (FontShapeGlyph){.glyph = glyph->Id,
                .pen_begin = (f32)(x * font->font->scale),
                .pen_end = (f32)((x + glyph->AdvanceX) * font->font->scale),
                .source = (u32)glyph->UserIdOrCodepointIndex,
                .x = (f32)((x + glyph->OffsetX) * font->font->scale),
                .y = (f32)((y + glyph->OffsetY) * font->font->scale),
                .advance = glyph->AdvanceX * font->font->scale,
                .rtl = run->direction == FONTSHAPER_DIRECTION_RTL};
            x += glyph->AdvanceX; y += glyph->AdvanceY;
        }
        run->glyph_end = shape->count;
        run->width = (f32)(x * font->font->scale);
        if (!isfinite(run->width) || run->width < 0) {
            shape->error = "invalid shaped run width"; goto fail;
        }
    }
    // Place script runs in visual order without losing logical source IDs.
    // Fontshaper already reverses glyph order *within* each RTL run.
    for (u32 p = 0; p < run_count;) {
        u32 end = p + 1;
        while (end < run_count && !runs[end].paragraph_start) end++;
        f32 width = 0;
        for (u32 r = p; r < end; r++) width += runs[r].width;
        f32 consumed = 0;
        for (u32 first = p; first < end;) {
            u32 last = first + 1;
            while (last < end && runs[last].direction == runs[first].direction) last++;
            f32 group_width = 0;
            for (u32 r = first; r < last; r++) group_width += runs[r].width;
            f32 group_x = shape->width + (runs[p].paragraph == FONTSHAPER_DIRECTION_RTL ?
                width - consumed - group_width : consumed);
            f32 run_consumed = 0;
            for (u32 r = first; r < last; r++) {
                f32 origin = group_x + (runs[r].direction == FONTSHAPER_DIRECTION_RTL ?
                    group_width - run_consumed - runs[r].width : run_consumed);
                for (u32 g = runs[r].glyph_first; g < runs[r].glyph_end; g++) {
                    shape->glyphs[g].x += origin;
                    shape->glyphs[g].pen_begin += origin;
                    shape->glyphs[g].pen_end += origin;
                }
                run_consumed += runs[r].width;
            }
            consumed += group_width;
            first = last;
        }
        shape->width += width;
        p = end;
    }
    if (!isfinite(shape->width)) { shape->error = "shaped width overflow"; goto fail; }
    return true;
fail:
    shape->count = 0; shape->width = 0;
    return false;
}
