#pragma once

#include <lib/grim/bp.h>
#include <lib/grim/logger.h>
#include <lib/grim/math.h>
#include <lib/grim/mem/arena.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// Only printable ASCII is cached. Compact glyph zero is the font's .notdef.
#define FONT_GLYPH_CAP 96

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
    TTFile cmap, loca, glyf, hmtx;
    i32 glyph_map[256]; // Compact indexes, never raw TrueType glyph IDs.
    u16 glyph_ids[FONT_GLYPH_CAP];
} TTFont;

typedef struct { f32 x, y; u8 on; } TTPoint;
typedef struct { TTPoint *pts; u32 n; } TTContour;
typedef struct { TTContour *cs; u32 nc; } TTGlyph;
typedef struct { f32 advance_width; } GlyphMetrics;
typedef struct { TTFont *font; GlyphMetrics *metrics; u32 glyph_count; } FontState;

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
    TTFile hhea = tt_table(file, 0x68686561), cmap = tt_table(file, 0x636D6170);
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
    tt_skip(&cmap, 2); u16 count = tt_r16(&cmap);
    tt_need(&cmap, (u32)count * 8);
    u32 priority = 0;
    for (u32 i = 0; i < count; i++) {
        u16 platform = tt_r16(&cmap), encoding = tt_r16(&cmap);
        u32 offset = tt_r32(&cmap);
        if (platform != 0 && !(platform == 3 && (encoding == 1 || encoding == 10))) continue;
        TTFile sub = tt_slice(cmap, offset, cmap.len - min(offset, cmap.len));
        u16 format = tt_r16(&sub);
        u32 length = 0, rank = 0;
        if (format == 4) { length = tt_r16(&sub); rank = 1; }
        else if (format == 12) { tt_skip(&sub, 2); length = tt_r32(&sub); rank = 2; }
        if (rank > priority) {
            f->cmap = tt_slice(sub, 0, length);
            priority = rank;
        }
    }
    tt_require(priority != 0, "no Unicode format 4 or 12 cmap");
    return f;
}

internal u32 tt_glyph_idx(const TTFont *f, u32 cp) {
    TTFile map = f->cmap;
    u16 format = tt_r16(&map);
    u32 gi = 0;
    if (format == 12) {
        tt_skip(&map, 10); u32 count = tt_r32(&map);
        tt_require(count <= (map.len - map.pos) / 12, "cmap groups");
        u32 lo = 0, hi = count;
        while (lo < hi) {
            u32 mid = lo + (hi - lo) / 2;
            TTFile group = map; group.pos += mid * 12;
            u32 start = tt_r32(&group), end = tt_r32(&group), first = tt_r32(&group);
            tt_require(start <= end, "cmap group range");
            if (cp < start) hi = mid;
            else if (cp > end) lo = mid + 1;
            else {
                tt_require(first <= UINT32_MAX - (cp - start), "cmap glyph overflow");
                gi = first + cp - start; break;
            }
        }
    } else {
        tt_skip(&map, 4); u16 seg_bytes = tt_r16(&map);
        tt_require(seg_bytes && !(seg_bytes & 1), "cmap segment count");
        u32 count = seg_bytes / 2;
        tt_skip(&map, 6); tt_need(&map, count * 8 + 2);
        u32 lo = 0, hi = count;
        while (lo < hi) {
            u32 mid = lo + (hi - lo) / 2;
            TTFile end = map; end.pos += mid * 2;
            if (tt_r16(&end) < cp) lo = mid + 1; else hi = mid;
        }
        if (lo < count) {
            TTFile segment = map; segment.pos += count * 2 + 2 + lo * 2;
            u32 start = tt_r16(&segment);
            if (cp >= start) {
                segment.pos += count * 2 - 2; i16 delta = tt_rs16(&segment);
                segment.pos += count * 2 - 2;
                u32 range_pos = segment.pos; u16 range = tt_r16(&segment);
                if (!range) gi = (u16)(cp + delta);
                else {
                    segment.pos = range_pos + range + (cp - start) * 2;
                    gi = tt_r16(&segment);
                    if (gi) gi = (u16)(gi + delta);
                }
            }
        }
    }
    tt_require(gi < f->glyph_count, "cmap glyph index");
    return gi;
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
            tt_require(flags & 2, "compound point matching is unsupported");
            f32 dx, dy;
            if (flags & 1) { dx = tt_rs16(&file); dy = tt_rs16(&file); }
            else { dx = (i8)tt_r8(&file); dy = (i8)tt_r8(&file); }
            f32 xx = 1, xy = 0, yx = 0, yy = 1;
            u32 transforms = !!(flags & 8) + !!(flags & 64) + !!(flags & 128);
            tt_require(transforms <= 1 && (flags & 0x1800) != 0x1800, "compound transform flags");
            if (flags & 128) {
                xx = tt_rs16(&file) / 16384.0f; xy = tt_rs16(&file) / 16384.0f;
                yx = tt_rs16(&file) / 16384.0f; yy = tt_rs16(&file) / 16384.0f;
            } else if (flags & 64) {
                xx = tt_rs16(&file) / 16384.0f; yy = tt_rs16(&file) / 16384.0f;
            } else if (flags & 8) { xx = yy = tt_rs16(&file) / 16384.0f; }
            if (flags & 0x0800) {
                f32 x = dx; dx = xx * x + yx * dy; dy = xy * x + yy * dy;
            }
            TTGlyph sub = tt_load_glyph_raw(a, f, child, depth + 1);
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
    FontState state = {.font = tt_open(a, path, size, points), .glyph_count = 1};
    TTFont *f = state.font;
    for (u32 cp = 32; cp <= 126; cp++) {
        u32 raw = tt_glyph_idx(f, cp), compact = 0;
        while (compact < state.glyph_count && f->glyph_ids[compact] != raw) compact++;
        if (compact == state.glyph_count) {
            tt_require(compact < FONT_GLYPH_CAP, "glyph cache capacity");
            f->glyph_ids[state.glyph_count++] = (u16)raw;
        }
        f->glyph_map[cp] = (i32)compact;
    }
    state.metrics = arena_alloc_aligned(a, GlyphMetrics, state.glyph_count);
    for (u32 i = 0; i < state.glyph_count; i++) {
        TTFile metrics = f->hmtx;
        metrics.pos = min((u32)f->glyph_ids[i], (u32)f->num_hmetrics - 1) * 4;
        state.metrics[i].advance_width = tt_r16(&metrics) * f->scale;
    }
    return state;
}

// Outlines are startup-only. Keep compact mapping/metrics after preprocessing.
internal void tt_release_data(TTFont *f) {
    if (f->data) munmap((void *)f->data, f->data_len);
    f->data = NULL; f->data_len = 0;
    f->cmap = f->loca = f->glyf = f->hmtx = (TTFile){0};
}
internal void font_destroy(FontState *state) {
    if (state->font) tt_release_data(state->font);
    *state = (FontState){0};
}
