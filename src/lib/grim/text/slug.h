#pragma once

#include <lib/grim/bp.h>
#include <lib/grim/logger.h>
#include <lib/grim/mem/arena.h>
#include <lib/grim/text/font.h>

// Slug glyph preprocessing.
//
// Flattens TrueType quadratic outlines into conservative, sorted band lists so
// the fragment shader can evaluate analytic coverage with a handful of curve
// tests instead of walking every contour. The result is a FontAtlas ready for
// a verbatim GPU upload (bounds, bands, curves, band words, in that order).

#define SLUG_BAND_EPS  (1.0f / 1024.0f)
#define SLUG_MAX_BANDS 16

typedef struct {
    f32 x0;
    f32 y0;
    f32 x1;
    f32 y1;
    f32 x2;
    f32 y2;
} SlugCrv;

// Flattens one glyph into quadratic Bézier curves, scaled and rounded to the
// half-float grid the shader fetches. Returns the curve count; writes curves
// into scratch and the glyph's ink bounds (scaled pixel space) into bounds.
internal u32 slug_flatten_glyph(rop(rw ScratchArena) scratch,
                                rop(ro TTGlyph) glyph,
                                f32 scale,
                                rop(rw SlugCrv) curves,
                                rop(rw f32) bounds_min,
                                rop(rw f32) bounds_max) {

    bounds_min[0] = 1e10f;
    bounds_min[1] = 1e10f;
    bounds_max[0] = -1e10f;
    bounds_max[1] = -1e10f;

    u32 curve_count = 0;
    for (u32 contour_idx = 0; contour_idx < glyph->contours_count; contour_idx++) {
        TTContour* contour     = &glyph->contours[contour_idx];
        u32        point_count = contour->pts_count;
        if (point_count < 2)
            continue;

        TTPoint* pts = contour->pts;
        for (u32 i = 0; i < point_count; i++) {
            TTPoint p    = pts[i];
            TTPoint next = pts[(i + 1) % point_count];

            SlugCrv curve;
            if (!p.on) {
                TTPoint prev = pts[(i + point_count - 1) % point_count];
                curve        = (SlugCrv){
                    prev.on ? prev.x : (prev.x + p.x) * 0.5f,
                    prev.on ? prev.y : (prev.y + p.y) * 0.5f,
                    p.x,
                    p.y,
                    next.on ? next.x : (p.x + next.x) * 0.5f,
                    next.on ? next.y : (p.y + next.y) * 0.5f,
                };
            } else if (next.on) {
                curve = (SlugCrv){p.x, p.y, (p.x + next.x) * 0.5f, (p.y + next.y) * 0.5f, next.x, next.y};
            } else {
                continue;
            }

            curve.x0 = (f32)(_Float16)(curve.x0 * scale);
            curve.y0 = (f32)(_Float16)(curve.y0 * scale);
            curve.x1 = (f32)(_Float16)(curve.x1 * scale);
            curve.y1 = (f32)(_Float16)(curve.y1 * scale);
            curve.x2 = (f32)(_Float16)(curve.x2 * scale);
            curve.y2 = (f32)(_Float16)(curve.y2 * scale);

            if (curve.x0 == curve.x1 && curve.x1 == curve.x2 && curve.y0 == curve.y1 && curve.y1 == curve.y2) {
                continue;
            }

            curves[curve_count++] = curve;

            bounds_min[0] = min(bounds_min[0], min(curve.x0, min(curve.x1, curve.x2)));
            bounds_min[1] = min(bounds_min[1], min(curve.y0, min(curve.y1, curve.y2)));
            bounds_max[0] = max(bounds_max[0], max(curve.x0, max(curve.x1, curve.x2)));
            bounds_max[1] = max(bounds_max[1], max(curve.y0, max(curve.y1, curve.y2)));
        }
    }
    return curve_count;
}

internal FontAtlas slug_build_atlas(rop(rw Arena) arena, rop(rw ScratchArena) scratch, rop(ro TTFont) font, f32 scale) {
    u32 glyph_count = font->glyph_count;

    // Upper bound on curves: each contour point emits at most one quadratic.
    // Computed once during font load.
    u64 curve_upper = font->total_points;

    // Loose upper bound for the band-word region: each curve can land in at
    // most 2 * SLUG_MAX_BANDS bands across both axes.
    u64 band_upper = (u64)glyph_count * SLUG_MAX_BANDS * 4 + curve_upper * 2 * SLUG_MAX_BANDS;

    // Build in scratch first (loose bounds for the growable regions), then
    // copy the exact used bytes into a tightly-sized atlas at the end.
    GlyphBounds* bounds = scratch_alloc_aligned(scratch, GlyphBounds, glyph_count);
    GlyphBands*  bands  = scratch_alloc_aligned(scratch, GlyphBands, glyph_count);
    PackedCurve* packed = scratch_alloc_aligned(scratch, PackedCurve, curve_upper);
    u32*         words  = scratch_alloc_aligned(scratch, u32, band_upper);

    u32 curve_cursor = 0;
    u32 band_cursor  = 0;

    for (u32 gi = 0; gi < glyph_count; gi++) {
        rop(ro TTGlyph) glyph = &font->glyphs[gi];
        bounds[gi]            = (GlyphBounds){0};
        bands[gi]             = (GlyphBands){0};

        if (!glyph->contours_count)
            continue;

        u32 curve_cap = 0;
        for (u32 ci = 0; ci < glyph->contours_count; ci++) {
            curve_cap += glyph->contours[ci].pts_count;
        }
        SlugCrv* curves = scratch_alloc_aligned(scratch, SlugCrv, curve_cap);
        f32      bounds_min[2];
        f32      bounds_max[2];
        u32      curve_count = slug_flatten_glyph(scratch, glyph, scale, curves, bounds_min, bounds_max);
        if (!curve_count)
            continue;

        u32 band_count = (u32)sqrtf((f32)curve_count);
        band_count     = clamp(band_count, 2u, (u32)SLUG_MAX_BANDS);

        f32 span_x = bounds_max[0] - bounds_min[0] + SLUG_BAND_EPS;
        f32 span_y = bounds_max[1] - bounds_min[1] + SLUG_BAND_EPS;
        f32 sx     = (f32)band_count / span_x;
        f32 sy     = (f32)band_count / span_y;

        bounds[gi] = (GlyphBounds){bounds_min[0], bounds_min[1], bounds_max[0], bounds_max[1]};
        bands[gi]  = (GlyphBands){
            .scale_x  = sx,
            .scale_y  = sy,
            .offset_x = -bounds_min[0] * sx,
            .offset_y = -bounds_min[1] * sy,
            .origin   = band_cursor,
            .counts   = ((u32)band_count << 16) | band_count, // v | h << 16
        };

        // Headers first (band_count*2 words per axis), then curve-index lists.
        u32 header_words = band_count * 4;
        u32 list_cursor  = header_words;

        for (u32 axis = 0; axis < 2; axis++) {
            u32 bin_axis = 1 - axis; // sorted by `axis`, binned by `bin_axis`
            f32 span     = bin_axis ? span_y : span_x;
            f32 lo       = bin_axis ? bounds_min[1] : bounds_min[0];

            // Sort curve indices descending by max extent along `axis`, so the
            // shader's early-out (`max(...) < -0.5 -> break`) is valid.
            u32* order = scratch_alloc_aligned(scratch, u32, curve_count);
            for (u32 i = 0; i < curve_count; i++)
                order[i] = i;
            for (u32 i = 1; i < curve_count; i++) {
                u32 key_idx = i;
                f32 key     = axis ? max(curves[i].y0, max(curves[i].y1, curves[i].y2))
                                   : max(curves[i].x0, max(curves[i].x1, curves[i].x2));
                u32 j       = i;
                while (j > 0) {
                    u32 prev = order[j - 1];
                    f32 pk   = axis ? max(curves[prev].y0, max(curves[prev].y1, curves[prev].y2))
                                    : max(curves[prev].x0, max(curves[prev].x1, curves[prev].x2));
                    if (pk >= key)
                        break;
                    order[j] = prev;
                    j--;
                }
                order[j] = key_idx;
            }

            for (u32 band = 0; band < band_count; band++) {
                f32 low  = lo + (f32)band * span / (f32)band_count - SLUG_BAND_EPS;
                f32 high = lo + ((f32)band + 1.0f) * span / (f32)band_count + SLUG_BAND_EPS;

                u32 header     = band_cursor + (axis * band_count + band) * 2;
                u32 list_start = band_cursor + list_cursor;
                u32 count      = 0;

                for (u32 i = 0; i < curve_count; i++) {
                    u32     idx  = order[i];
                    SlugCrv c    = curves[idx];
                    f32     cmax = bin_axis ? max(c.y0, max(c.y1, c.y2)) : max(c.x0, max(c.x1, c.x2));
                    f32     cmin = bin_axis ? min(c.y0, min(c.y1, c.y2)) : min(c.x0, min(c.x1, c.x2));
                    if (cmax < low || cmin > high)
                        continue;
                    words[list_start + count] = curve_cursor + idx;
                    count++;
                }

                words[header]     = count;
                words[header + 1] = list_cursor; // relative offset from origin
                list_cursor += count;
            }
        }

        // Pack curves into half2 triplets.
        for (u32 i = 0; i < curve_count; i++) {
            SlugCrv  c = curves[i];
            u16      x0, y0, x1, y1, x2, y2;
            _Float16 h;
            h = (_Float16)c.x0;
            memory_copy(&x0, &h, 2);
            h = (_Float16)c.y0;
            memory_copy(&y0, &h, 2);
            h = (_Float16)c.x1;
            memory_copy(&x1, &h, 2);
            h = (_Float16)c.y1;
            memory_copy(&y1, &h, 2);
            h = (_Float16)c.x2;
            memory_copy(&x2, &h, 2);
            h = (_Float16)c.y2;
            memory_copy(&y2, &h, 2);
            packed[curve_cursor + i] = (PackedCurve){
                .p0 = ((u32)y0 << 16) | x0,
                .p1 = ((u32)y1 << 16) | x1,
                .p2 = ((u32)y2 << 16) | x2,
            };
        }

        curve_cursor += curve_count;
        band_cursor += header_words + (list_cursor - header_words);
    }

    // Exact-sized atlas: bounds, bands, curves, band words, contiguous.
    u64 bounds_bytes = (u64)glyph_count * sizeof(GlyphBounds);
    u64 bands_bytes  = (u64)glyph_count * sizeof(GlyphBands);
    u64 curves_bytes = (u64)curve_cursor * sizeof(PackedCurve);
    u64 band_bytes   = (u64)band_cursor * sizeof(u32);

    u64 bounds_off = 0;
    u64 bands_off  = (bounds_off + bounds_bytes + 15) & ~(u64)15;
    u64 curves_off = (bands_off + bands_bytes + 15) & ~(u64)15;
    u64 band_off   = (curves_off + curves_bytes + 15) & ~(u64)15;
    u64 total      = (band_off + band_bytes + 15) & ~(u64)15;

    u8* font_data = arena_alloc_aligned(arena, u8, total);

    memory_copy(font_data + bounds_off, bounds, bounds_bytes);
    memory_copy(font_data + bands_off, bands, bands_bytes);
    memory_copy(font_data + curves_off, packed, curves_bytes);
    memory_copy(font_data + band_off, words, band_bytes);

    scratch_reset(scratch);

    FontAtlas atlas = {
        .data            = font_data,
        .total_bytes     = total,
        .glyph_count     = glyph_count,
        .curve_count     = curve_cursor,
        .band_word_count = band_cursor,
    };

    return atlas;
}
