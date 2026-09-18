#pragma once

#include <lib/grim/bp.h>
#include <lib/grim/logger.h>
#include <lib/grim/markdown/font.h>
#include <lib/grim/mem/arena.h>

// Slug quadratic outlines and conservative, sorted band lists.
#define SLUG_BAND_EPS (1.0f / 1024.0f)
#define SLUG_MAX_BANDS 16

typedef struct {
    f32 x0, y0, x1, y1, x2, y2;
} SlugCrv;

typedef struct {
    u32 band_origin;
    u32 hband_n, vband_n; // Counts, not last band indexes.
} SlugGlyphMeta;

typedef struct {
    SlugGlyphMeta *meta;
    u32 glyph_n, glyph_c;
    u32 *curves; // Three packed half2 words per quadratic, no padding.
    u32 *bands;  // Two-word (count, relative word offset) headers, then curve indexes.
    u32 curve_capacity, band_capacity; // Allocated words.
    u32 curve_count, band_words;
    f32 *glyph_bbox_min_x, *glyph_bbox_min_y;
    f32 *glyph_bbox_max_x, *glyph_bbox_max_y;
} SlugCtx;

internal void slug_reserve_words(u32 **data, u32 *capacity, u32 needed) {
    if (needed <= *capacity) return;
    u32 cap = *capacity ? *capacity : 1024;
    while (cap < needed) cap = cap > UINT32_MAX / 2 ? needed : cap * 2;
    tt_require((u64)cap * sizeof(u32) <= SIZE_MAX, "Slug buffer size overflow");
    u32 *grown = realloc(*data, (usize)cap * sizeof(u32));
    tt_require(grown != NULL, "Slug buffer allocation");
    *data = grown;
    *capacity = cap;
}

internal u16 slug_half_bits(f32 value) {
    _Float16 half = (_Float16)value;
    u16 bits;
    memory_copy(&bits, &half, sizeof(bits));
    return bits;
}

internal u32 slug_half2(f32 x, f32 y) {
    return (u32)slug_half_bits(x) | ((u32)slug_half_bits(y) << 16);
}

internal void slug_ctx_init(rop(rw Arena) a, rop(rw SlugCtx) sc,
                           u32 glyph_count) {
    memory_zero_struct(sc);
    sc->glyph_c = glyph_count;
    sc->meta = arena_alloc_aligned(a, SlugGlyphMeta, (usize)glyph_count);
    sc->glyph_bbox_min_x = arena_alloc_aligned(a, f32, (usize)glyph_count);
    sc->glyph_bbox_min_y = arena_alloc_aligned(a, f32, (usize)glyph_count);
    sc->glyph_bbox_max_x = arena_alloc_aligned(a, f32, (usize)glyph_count);
    sc->glyph_bbox_max_y = arena_alloc_aligned(a, f32, (usize)glyph_count);
    memory_zero(sc->meta, (usize)glyph_count * sizeof(*sc->meta));
}

internal f32 slug_curve_max(SlugCrv c, u32 axis) {
    return axis ? fmaxf(c.y0, fmaxf(c.y1, c.y2))
                : fmaxf(c.x0, fmaxf(c.x1, c.x2));
}

internal f32 slug_curve_min(SlugCrv c, u32 axis) {
    return axis ? fminf(c.y0, fminf(c.y1, c.y2))
                : fminf(c.x0, fminf(c.x1, c.x2));
}

// The caller resets scratch after each glyph; only packed data survives.
internal void slug_preprocess_glyph(rop(rw Arena) scratch, rop(rw SlugCtx) sc,
                                    rop(ro TTFont) font, u32 gi) {
    tt_require(gi < sc->glyph_c, "Slug glyph cache index");
    TTGlyph g = tt_get_glyph(scratch, font, gi);
    if (g.nc == 0) return;

    u32 curve_cap = 0;
    for (u32 ci = 0; ci < g.nc; ci++) curve_cap += g.cs[ci].n;
    SlugCrv *curves = arena_alloc_aligned(scratch, SlugCrv, (usize)curve_cap);
    u32 curve_n = 0;
    f32 bounds_min[2] = {1e10f, 1e10f};
    f32 bounds_max[2] = {-1e10f, -1e10f};
    for (u32 ci = 0; ci < g.nc; ci++) {
        TTContour *contour = &g.cs[ci];
        if (contour->n < 2) continue;
        for (u32 i = 0; i < contour->n; i++) {
            TTPoint p = contour->pts[i];
            TTPoint next = contour->pts[(i + 1) % contour->n];
            SlugCrv c;
            if (!p.on) {
                TTPoint prev = contour->pts[(i + contour->n - 1) % contour->n];
                // Each off-curve point contributes one quadratic. This also
                // handles cyclic contours starting off-curve and implicit midpoints.
                c = (SlugCrv){prev.on ? prev.x : (prev.x + p.x) * 0.5f,
                              prev.on ? prev.y : (prev.y + p.y) * 0.5f,
                              p.x, p.y,
                              next.on ? next.x : (p.x + next.x) * 0.5f,
                              next.on ? next.y : (p.y + next.y) * 0.5f};
            } else if (next.on) {
                // Midpoint control makes this a linear polynomial, including at endpoints.
                c = (SlugCrv){p.x, p.y, (p.x + next.x) * 0.5f,
                              (p.y + next.y) * 0.5f, next.x, next.y};
            } else {
                continue;
            }
            // Bin and sort the exact values the shader will fetch. Otherwise
            // half-float rounding can move a curve outside its CPU-side band.
            c.x0 = (f32)(_Float16)c.x0; c.y0 = (f32)(_Float16)c.y0;
            c.x1 = (f32)(_Float16)c.x1; c.y1 = (f32)(_Float16)c.y1;
            c.x2 = (f32)(_Float16)c.x2; c.y2 = (f32)(_Float16)c.y2;
            tt_require(isfinite(c.x0) && isfinite(c.y0) && isfinite(c.x1) &&
                       isfinite(c.y1) && isfinite(c.x2) && isfinite(c.y2),
                       "outline exceeds half-float range");
            if (c.x0 == c.x1 && c.x1 == c.x2 && c.y0 == c.y1 && c.y1 == c.y2)
                continue;
            curves[curve_n++] = c;
            for (u32 axis = 0; axis < 2; axis++) {
                bounds_min[axis] = fminf(bounds_min[axis], slug_curve_min(c, axis));
                bounds_max[axis] = fmaxf(bounds_max[axis], slug_curve_max(c, axis));
            }
        }
    }
    if (!curve_n) return;

    u32 band_n = (u32)sqrtf((f32)curve_n);
    if (band_n < 2) band_n = 2;
    if (band_n > SLUG_MAX_BANDS) band_n = SLUG_MAX_BANDS;
    u32 counts[SLUG_MAX_BANDS * 2] = {0};
    u32 *sorted = arena_alloc_aligned(scratch, u32, (usize)curve_n * 2);
    f32 band_size[2] = {(bounds_max[0] - bounds_min[0] + SLUG_BAND_EPS) / band_n,
                        (bounds_max[1] - bounds_min[1] + SLUG_BAND_EPS) / band_n};
    u64 band_words = band_n * 4;
    for (u32 axis = 0; axis < 2; axis++) {
        // Horizontal rays sort by max x, vertical rays by max y. Sorting
        // once per direction preserves the early-exit order in every band.
        u32 *order = sorted + axis * curve_n;
        for (u32 i = 0; i < curve_n; i++) {
            u32 j = i;
            f32 key = slug_curve_max(curves[i], axis);
            while (j && slug_curve_max(curves[order[j - 1]], axis) < key) {
                order[j] = order[j - 1];
                j--;
            }
            order[j] = i;
        }
        u32 bin_axis = 1 - axis;
        for (u32 band = 0; band < band_n; band++) {
            f32 low = bounds_min[bin_axis] + band * band_size[bin_axis] - SLUG_BAND_EPS;
            f32 high = bounds_min[bin_axis] + (band + 1) * band_size[bin_axis] + SLUG_BAND_EPS;
            u32 count = 0;
            for (u32 i = 0; i < curve_n; i++) {
                SlugCrv c = curves[i];
                if (slug_curve_max(c, bin_axis) >= low && slug_curve_min(c, bin_axis) <= high)
                    count++;
            }
            counts[axis * band_n + band] = count;
            band_words += count;
        }
    }

    // Use full-width linear offsets; validate arithmetic before publishing.
    tt_require(curve_n <= UINT32_MAX / 3 - sc->curve_count &&
               band_words <= UINT32_MAX - sc->band_words,
               "glyph exceeds Slug buffer limits");
    slug_reserve_words(&sc->curves, &sc->curve_capacity, (sc->curve_count + curve_n) * 3);
    slug_reserve_words(&sc->bands, &sc->band_capacity, sc->band_words + (u32)band_words);
    SlugGlyphMeta *meta = &sc->meta[gi];
    meta->band_origin = sc->band_words;
    meta->hband_n = meta->vband_n = band_n;
    sc->glyph_bbox_min_x[gi] = bounds_min[0]; sc->glyph_bbox_min_y[gi] = bounds_min[1];
    sc->glyph_bbox_max_x[gi] = bounds_max[0]; sc->glyph_bbox_max_y[gi] = bounds_max[1];

    u32 location = band_n * 4;
    for (u32 axis = 0; axis < 2; axis++) {
        u32 bin_axis = 1 - axis;
        for (u32 band = 0; band < band_n; band++) {
            u32 header = sc->band_words + (axis * band_n + band) * 2;
            sc->bands[header] = counts[axis * band_n + band];
            sc->bands[header + 1] = location;
            f32 low = bounds_min[bin_axis] + band * band_size[bin_axis] - SLUG_BAND_EPS;
            f32 high = bounds_min[bin_axis] + (band + 1) * band_size[bin_axis] + SLUG_BAND_EPS;
            for (u32 i = 0; i < curve_n; i++) {
                u32 curve = sorted[axis * curve_n + i];
                SlugCrv c = curves[curve];
                if (slug_curve_max(c, bin_axis) < low || slug_curve_min(c, bin_axis) > high)
                    continue;
                sc->bands[sc->band_words + location++] = sc->curve_count + curve;
            }
        }
    }
    for (u32 i = 0; i < curve_n; i++) {
        SlugCrv c = curves[i];
        u32 *dst = sc->curves + (sc->curve_count + i) * 3;
        dst[0] = slug_half2(c.x0, c.y0);
        dst[1] = slug_half2(c.x1, c.y1);
        dst[2] = slug_half2(c.x2, c.y2);
    }
    sc->curve_count += curve_n;
    sc->band_words += (u32)band_words;
    sc->glyph_n++;
}
