#pragma once

#include <fcntl.h>
#include <lib/grim/assert.h>
#include <lib/grim/bp.h>
#include <lib/grim/gfx/types.h>
#include <lib/grim/intrinsics.h>
#include <lib/grim/logger.h>
#include <lib/grim/math.h>
#include <lib/grim/mem/arena.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// TrueType outline parsing.
//
// A font is mmap'd once, parsed into a TTFont, and its glyph outlines are
// turned into a FontAtlas by slug.h. The mmap'd file is released once
// preprocessing is done.

#define TT_TAG_head 0x68656164
#define TT_TAG_maxp 0x6D617870
#define TT_TAG_hhea 0x68686561
#define TT_TAG_loca 0x6C6F6361
#define TT_TAG_glyf 0x676C7966
#define TT_TAG_hmtx 0x686D7478

// glyf simple-glyph point flag bits.
#define TT_POINT_ON_CURVE 0x01
#define TT_POINT_X_SHORT  0x02
#define TT_POINT_Y_SHORT  0x04
#define TT_POINT_REPEAT   0x08
#define TT_POINT_X_SAME   0x10
#define TT_POINT_Y_SAME   0x20

// glyf compound-glyph component flag bits.
#define TT_COMP_ARG_WORDS     0x0001
#define TT_COMP_ARGS_XY       0x0002
#define TT_COMP_ROUND_XY      0x0004
#define TT_COMP_SCALE         0x0008
#define TT_COMP_MORE          0x0020
#define TT_COMP_X_SCALE       0x0040
#define TT_COMP_2x2           0x0080
#define TT_COMP_INSTRUCTIONS  0x0100
#define TT_COMP_USE_METRICS   0x0200
#define TT_COMP_OVERLAP       0x0400
#define TT_COMP_SCALED_OFFSET 0x0800

#define TT_F2DOT14_SCALE (1.0f / 16384.0f)

// TODO rename to Reader or Cursor or something
typedef struct {
    const u8* d;
    u32       len;
    u32       pos;
} TTFile;

typedef struct {
    const u8* data;
    u32       data_len;
    u32       glyph_count;
    u16       upem;
    u16       num_hmetrics;
    i16       loca_fmt; // 0 = short offsets, 1 = long.
    f32       scale;
    f32       ascent;
    f32       descent;
    f32       line_gap;
    f32       line_height;
    TTFile    loca;
    TTFile    glyf;
    TTFile    hmtx;
} TTFont;

typedef struct {
    f32 x;
    f32 y;
    u8  on;
} TTPoint;

typedef struct {
    TTPoint* pts;
    u32      pts_count;
} TTContour;

typedef struct {
    TTContour* contours;
    u32        contours_count;
} TTGlyph;

internal u32 tt_read_u32(TTFile* font) {
    assert(SCOPE_FONT_LOAD, font->pos <= font->len && font->len - font->pos >= 4);
    const u8* p = font->d + font->pos;
    font->pos += 4;
    u32 value;
    memcpy(&value, p, 4);
    return __builtin_bswap32(value);
}

internal u16 tt_read_u16(TTFile* font) {
    assert(SCOPE_FONT_LOAD, font->pos <= font->len && font->len - font->pos >= 2);
    const u8* p = font->d + font->pos;
    font->pos += 2;
    u16 value;
    memcpy(&value, p, 2);
    return __builtin_bswap16(value);
}

internal i16 tt_read_i16(TTFile* font) {
    return (i16)tt_read_u16(font);
}

internal u8 tt_read_u8(TTFile* font) {
    assert(SCOPE_FONT_LOAD, font->pos < font->len);
    return font->d[font->pos++];
}

internal TTFile tt_slice(TTFile font, u32 off, u32 len) {
    assert(SCOPE_FONT_LOAD, off <= font.len && len <= font.len - off);
    return (TTFile){font.d + off, len, 0};
}

// tables must be able to fit 6 TTFiles
// [0] = loca, [1] = glyf, [2] = hmtx, [3] = head, [4] = maxp, [5] = hhea
internal void tt_table_load(TTFile file, rop(rw TTFile) tables) {
    TTFile dir = file;
    assert(SCOPE_FONT_LOAD, tt_read_u32(&dir) == 0x00010000);
    u16 count = tt_read_u16(&dir);
    dir.pos += 6; // searchRange/entrySelector/rangeShift
    assert(SCOPE_FONT_LOAD, (u32)count * 16 <= dir.len - dir.pos);

    u32 tables_found = 0;

    for (u32 i = 0; i < count; i++) {
        // Each directory record is 16 contiguous bytes: tag, checkSum, offset, length.
        // Full reverse swaps the field order too, so read them back-to-front.
        u32 record[4];
        simd_byte_reverse(dir.d + dir.pos, (u8*)record, 16);
        dir.pos += 16;

        // clang-format off
        if (record[3] == TT_TAG_loca) { tables[0] = tt_slice(file, record[1], record[0]); tables_found |= 1 << 0; }
        if (record[3] == TT_TAG_glyf) { tables[1] = tt_slice(file, record[1], record[0]); tables_found |= 1 << 1; }
        if (record[3] == TT_TAG_hmtx) { tables[2] = tt_slice(file, record[1], record[0]); tables_found |= 1 << 2; }
        if (record[3] == TT_TAG_head) { tables[3] = tt_slice(file, record[1], record[0]); tables_found |= 1 << 3; }
        if (record[3] == TT_TAG_maxp) { tables[4] = tt_slice(file, record[1], record[0]); tables_found |= 1 << 4; }
        if (record[3] == TT_TAG_hhea) { tables[5] = tt_slice(file, record[1], record[0]); tables_found |= 1 << 5; }
        // clang-format on

        if (tables_found == 0b111111) {
            return;
        }
    }

    assert(SCOPE_FONT_LOAD, false);
}

internal TTFont* tt_open(Arena* a, const char* path, f32 size, u32 points) {
    i32 fd = open(path, O_RDONLY | O_CLOEXEC);
    assert(SCOPE_FONT_LOAD, fd >= 0);

    struct stat st;
    assert(SCOPE_FONT_LOAD, fstat(fd, &st) == 0 && st.st_size > 0 && (u64)st.st_size <= u32_MAX);
    const u8* data = mmap(NULL, (usize)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    assert(SCOPE_FONT_LOAD, data != MAP_FAILED);

    TTFont* font = arena_alloc_aligned(a, TTFont, 1);
    *font        = (TTFont){.data = data, .data_len = (u32)st.st_size};
    TTFile file  = {data, font->data_len, 0};

    TTFile tables[6] = {0};

    tt_table_load(file, tables);

    TTFile head = tables[3];
    TTFile maxp = tables[4];
    TTFile hhea = tables[5];

    head.pos += 18;
    font->upem = tt_read_u16(&head);
    head.pos += 30;
    font->loca_fmt = tt_read_i16(&head);
    assert(SCOPE_FONT_LOAD, font->upem >= 16 && font->upem <= 16384 && (font->loca_fmt == 0 || font->loca_fmt == 1));

    maxp.pos += 4;
    font->glyph_count = tt_read_u16(&maxp);

    hhea.pos += 4;
    i16 asc  = tt_read_i16(&hhea);
    i16 desc = tt_read_i16(&hhea);
    i16 gap  = tt_read_i16(&hhea);
    hhea.pos += 24;
    font->num_hmetrics = tt_read_u16(&hhea);
    assert(SCOPE_FONT_LOAD,
           font->glyph_count && font->num_hmetrics && font->num_hmetrics <= font->glyph_count && asc > desc &&
               isfinite(size) && size > 0);

    font->scale       = points ? (size * (96.0f / 72.0f)) / font->upem : size / (f32)(asc - desc);
    font->ascent      = asc * font->scale;
    font->descent     = desc * font->scale;
    font->line_gap    = gap * font->scale;
    font->line_height = font->ascent - font->descent + font->line_gap;
    assert(SCOPE_FONT_LOAD, isfinite(font->line_height) && font->line_height > 0);

    font->loca = tables[0];
    font->glyf = tables[1];
    font->hmtx = tables[2];

    assert(SCOPE_FONT_LOAD, (font->glyph_count + 1) * (font->loca_fmt ? 4 : 2) <= font->loca.len - font->loca.pos);
    assert(SCOPE_FONT_LOAD,
           font->num_hmetrics * 4 + (font->glyph_count - font->num_hmetrics) * 2 <= font->hmtx.len - font->hmtx.pos);
    return font;
}

internal u32 tt_glyph_offset(rop(ro TTFont) font, u32 gi) {
    assert(SCOPE_FONT_LOAD, gi <= font->glyph_count);

    u32 offset = 0;
    if (font->loca_fmt) {
        memcpy(&offset, font->loca.d + gi * 4, 4);
        offset = __builtin_bswap32(offset);
    } else {
        u16 half;
        memcpy(&half, font->loca.d + gi * 2, 2);
        offset = __builtin_bswap16(half) * 2;
    }

    assert(SCOPE_FONT_LOAD, offset <= font->glyf.len);

    return offset;
}

internal TTGlyph
tt_load_glyph_raw(rop(rw Arena) arena, rop(rw ScratchArena) scratch, rop(ro TTFont) font, u32 gi, u32 depth);
internal TTGlyph tt_load_simple_glyph(
    rop(rw Arena) arena, rop(rw ScratchArena) scratch, rop(ro TTFont) font, TTFile file, u32 contours_count);
internal TTGlyph
tt_load_compound_glyph(rop(rw Arena) arena, rop(rw ScratchArena) scratch, rop(ro TTFont) font, TTFile file, u32 depth);

internal TTGlyph tt_load_simple_glyph(
    rop(rw Arena) arena, rop(rw ScratchArena) scratch, rop(ro TTFont) font, TTFile file, u32 contours_count) {

    TTContour* contours = arena_alloc_aligned(arena, TTContour, contours_count);

    // TODO this should be a ckpt but that's not impl for scratch
    u64 scratch_start = scratch->len;

    // reversed array of ends
    u16* ends = scratch_alloc_aligned(scratch, u16, contours_count);
    simd_byte_reverse(file.d + file.pos, (u8*)ends, contours_count * 2);
    file.pos += contours_count * 2;

    u32 point_count = (u32)ends[0] + 1;
    file.pos += tt_read_u16(&file); // instructions

    u8* flags        = scratch_alloc_aligned(scratch, u8, point_count);
    u32 x_byte_count = 0;
    for (u32 i = 0; i < point_count;) {
        u8  flag   = tt_read_u8(&file);
        u32 repeat = (flag & TT_POINT_REPEAT) ? tt_read_u8(&file) : 0;

        // need to count self
        repeat += 1;

        // if short_bit, +1
        // if not short, not same, +2
        // if not short, same, +0

        u32 shrt = (flag & TT_POINT_X_SHORT) > 0;
        u32 same = (flag & TT_POINT_X_SAME) > 0;

        u32 x_delta = ((!same && !shrt) << 1) | // 2 bit
                      shrt;                     // 1 bit

        x_byte_count += x_delta * repeat;

        assert(SCOPE_FONT_LOAD, i + repeat <= point_count);
        memset(flags + i, flag, repeat);
        i += repeat;
    }

    TTPoint* pts = arena_alloc_aligned(arena, TTPoint, point_count);

    rwp(ro u8) x_ptr = file.d + file.pos;
    rwp(ro u8) y_ptr = x_ptr + x_byte_count;

    i32 previous_x     = 0;
    i32 previous_y     = 0;
    u32 contour_idx    = 0;
    u32 contours_sofar = 0;
    for (u32 i = 0; i < point_count; i++) {
        i32 delta_x = 0;
        i32 delta_y = 0;

        u32 x_same = flags[i] & TT_POINT_X_SAME;
        if (flags[i] & TT_POINT_X_SHORT) {
            // single byte, same bit stores sign
            delta_x = x_ptr[0];
            delta_x *= (x_same ? 1 : -1);
            x_ptr += 1;
        } else if (!x_same) {
            // double byte, big-endian i16
            i16 dx;
            memcpy(&dx, x_ptr, 2);
            delta_x = (i16)__builtin_bswap16((u16)dx);
            x_ptr += 2;
        }
        // else: 0, implicit

        u32 y_same = flags[i] & TT_POINT_Y_SAME;
        if (flags[i] & TT_POINT_Y_SHORT) {
            // single byte, same bit stores sign
            delta_y = y_ptr[0];
            delta_y *= (y_same ? 1 : -1);
            y_ptr += 1;
        } else if (!y_same) {
            // double byte, big-endian i16
            i16 dy;
            memcpy(&dy, y_ptr, 2);
            delta_y = (i16)__builtin_bswap16((u16)dy);
            y_ptr += 2;
        }
        // else: 0, implicit

        previous_x += delta_x;
        previous_y += delta_y;

        assert(SCOPE_FONT_LOAD, previous_x >= i16_MIN && previous_x <= i16_MAX);
        assert(SCOPE_FONT_LOAD, previous_y >= i16_MIN && previous_y <= i16_MAX);

        pts[i].x  = (f32)previous_x;
        pts[i].y  = (f32)previous_y;
        pts[i].on = flags[i] & TT_POINT_ON_CURVE;

        if (i == ends[contours_count - contour_idx - 1]) {
            contours[contour_idx] = (TTContour){
                .pts       = pts + contours_sofar,
                .pts_count = i + 1 - contours_sofar,
            };

            contours_sofar = i + 1;
            contour_idx++;
        }
    }

    memset(scratch->data + scratch_start, 0, scratch->len - scratch_start);
    scratch->len = scratch_start;

    return (TTGlyph){
        .contours       = contours,
        .contours_count = contours_count,
    };
}

internal TTGlyph
tt_load_compound_glyph(rop(rw Arena) arena, rop(rw ScratchArena) scratch, rop(ro TTFont) font, TTFile file, u32 depth) {
    TTGlyph glyph       = {0};
    u32     cap         = 8;
    glyph.contours      = arena_alloc_aligned(arena, TTContour, cap);
    u16 component_flags = 0;

    do {
        component_flags = tt_read_u16(&file);
        u32 child       = tt_read_u16(&file);
        i32 arg1, arg2;
        if (component_flags & TT_COMP_ARG_WORDS) {
            arg1 = component_flags & TT_COMP_ARGS_XY ? tt_read_i16(&file) : tt_read_u16(&file);
            arg2 = component_flags & TT_COMP_ARGS_XY ? tt_read_i16(&file) : tt_read_u16(&file);
        } else {
            arg1 = component_flags & TT_COMP_ARGS_XY ? (i8)tt_read_u8(&file) : tt_read_u8(&file);
            arg2 = component_flags & TT_COMP_ARGS_XY ? (i8)tt_read_u8(&file) : tt_read_u8(&file);
        }

        f32 dx = component_flags & TT_COMP_ARGS_XY ? arg1 : 0;
        f32 dy = component_flags & TT_COMP_ARGS_XY ? arg2 : 0;
        f32 xx = 1, xy = 0, yx = 0, yy = 1;

        u32 transform_count = !!(component_flags & TT_COMP_SCALE) + !!(component_flags & TT_COMP_X_SCALE) +
                              !!(component_flags & TT_COMP_2x2);
        assert(SCOPE_FONT_LOAD, transform_count <= 1 && (component_flags & 0x1800) != 0x1800);

        if (component_flags & TT_COMP_2x2) {
            xx = tt_read_i16(&file) * TT_F2DOT14_SCALE;
            xy = tt_read_i16(&file) * TT_F2DOT14_SCALE;
            yx = tt_read_i16(&file) * TT_F2DOT14_SCALE;
            yy = tt_read_i16(&file) * TT_F2DOT14_SCALE;
        } else if (component_flags & TT_COMP_X_SCALE) {
            xx = tt_read_i16(&file) * TT_F2DOT14_SCALE;
            yy = tt_read_i16(&file) * TT_F2DOT14_SCALE;
        } else if (component_flags & TT_COMP_SCALE) {
            xx = yy = tt_read_i16(&file) * TT_F2DOT14_SCALE;
        }

        if ((component_flags & TT_COMP_ARGS_XY) && (component_flags & TT_COMP_SCALED_OFFSET)) {
            f32 x = dx;
            dx    = xx * x + yx * dy;
            dy    = xy * x + yy * dy;
        }

        TTGlyph sub = tt_load_glyph_raw(arena, scratch, font, child, depth + 1);

        if (!(component_flags & TT_COMP_ARGS_XY)) {
            TTPoint* parent    = NULL;
            TTPoint* component = NULL;
            u32      at        = (u32)arg1;
            for (u32 ci = 0; ci < glyph.contours_count; ci++) {
                if (at < glyph.contours[ci].pts_count) {
                    parent = &glyph.contours[ci].pts[at];
                    break;
                }
                at -= glyph.contours[ci].pts_count;
            }
            at = (u32)arg2;
            for (u32 ci = 0; ci < sub.contours_count; ci++) {
                if (at < sub.contours[ci].pts_count) {
                    component = &sub.contours[ci].pts[at];
                    break;
                }
                at -= sub.contours[ci].pts_count;
            }
            assert(SCOPE_FONT_LOAD, parent && component);
            dx = parent->x - (xx * component->x + yx * component->y);
            dy = parent->y - (xy * component->x + yy * component->y);
        } else if (component_flags & TT_COMP_ROUND_XY) {
            dx = roundf(dx);
            dy = roundf(dy);
        }

        assert(SCOPE_FONT_LOAD, sub.contours_count <= 65535 - glyph.contours_count);
        if (glyph.contours_count + sub.contours_count > cap) {
            while (cap < glyph.contours_count + sub.contours_count) {
                cap *= 2;
            }
            TTContour* old = glyph.contours;
            glyph.contours = arena_alloc_aligned(arena, TTContour, cap);
            memory_copy(glyph.contours, old, glyph.contours_count * sizeof(*old));
        }
        for (u32 ci = 0; ci < sub.contours_count; ci++) {
            for (u32 pi = 0; pi < sub.contours[ci].pts_count; pi++) {
                TTPoint* p = &sub.contours[ci].pts[pi];
                f32      x = p->x, y = p->y;
                p->x = xx * x + yx * y + dx;
                p->y = xy * x + yy * y + dy;
            }
            glyph.contours[glyph.contours_count++] = sub.contours[ci];
        }
    } while (component_flags & TT_COMP_MORE);

    if (component_flags & TT_COMP_INSTRUCTIONS) {
        file.pos += tt_read_u16(&file);
    }
    return glyph;
}

internal TTGlyph
tt_load_glyph_raw(rop(rw Arena) arena, rop(rw ScratchArena) scratch, rop(ro TTFont) font, u32 gi, u32 depth) {
    assert(SCOPE_FONT_LOAD, gi < font->glyph_count && depth <= 16);

    u32 off = tt_glyph_offset(font, gi);
    u32 end = tt_glyph_offset(font, gi + 1);

    assert(SCOPE_FONT_LOAD, end >= off);

    if (off == end) {
        return (TTGlyph){0}; // empty glyph (e.g. space)
    }

    TTFile file           = tt_slice(font->glyf, off, end - off);
    i16    contours_count = tt_read_i16(&file);
    file.pos += 8; // xMin/yMin/xMax/yMax, already in bounds above.

    if (contours_count > 0) {
        return tt_load_simple_glyph(arena, scratch, font, file, (u32)contours_count);
    } else if (contours_count == -1) {
        return tt_load_compound_glyph(arena, scratch, font, file, depth);
    }
    assert(SCOPE_FONT_LOAD, contours_count == 0);
    return (TTGlyph){0};
}

internal TTGlyph tt_get_glyph(rop(rw Arena) arena, rop(rw ScratchArena) scratch, rop(ro TTFont) font, u32 gi) {
    TTGlyph glyph = tt_load_glyph_raw(arena, scratch, font, gi, 0);
    for (u32 ci = 0; ci < glyph.contours_count; ci++) {
        for (u32 pi = 0; pi < glyph.contours[ci].pts_count; pi++) {
            glyph.contours[ci].pts[pi].x *= font->scale;
            glyph.contours[ci].pts[pi].y *= font->scale;
        }
    }
    return glyph;
}

internal void tt_release_data(rop(rw TTFont) font) {
    if (font->data) {
        munmap((void*)font->data, font->data_len);
    }
    font->data     = NULL;
    font->data_len = 0;
    font->loca     = (TTFile){0};
    font->glyf     = (TTFile){0};
    font->hmtx     = (TTFile){0};
}

typedef struct _CompoundQueueElement {
    u32 dst_idx;
    u32 glyph_idx;
} _CompoundQueueElement;

TTGlyph* tt_load_font(rop(rw Arena) arena, rop(rw ScratchArena) scratch, rop(rw TTFont) font) {
    u32                    queue_length   = 0;
    _CompoundQueueElement* compound_queue = scratch_alloc_aligned(scratch, _CompoundQueueElement, font->glyph_count);
    TTGlyph*               glyphs         = arena_alloc_aligned(arena, TTGlyph, font->glyph_count);

    // NOTE local const to avoid pointer aliasing interfering with loop unswitching
    const u32 loca_fmt = font->loca_fmt;

    u32 current_offset = 0;
    for (u32 glyph_idx = 0; glyph_idx < font->glyph_count; glyph_idx++) {
        u32 off = current_offset;
        u32 end = 0;

        // PERF make sure this is unswitched by the compiler
        if (loca_fmt) {
            memcpy(&end, font->loca.d + glyph_idx * 4, 4);
            end = __builtin_bswap32(end);
        } else {
            u16 half;
            memcpy(&half, font->loca.d + glyph_idx * 2, 2);
            end = __builtin_bswap16(half) * 2;
        }

        current_offset = end;

        if (off == end) {
            // NOTE we do a trick here where we set the pointer to != 0 so we can verify we've touched this glyph
            glyphs[glyph_idx] = (TTGlyph){.contours = (void*)1}; // empty glyph (e.g. space)
            continue;
        }

        TTFile file           = tt_slice(font->glyf, off, end - off);
        i16    contours_count = tt_read_i16(&file);
        file.pos += 8; // xMin/yMin/xMax/yMax, already in bounds above.

        if (contours_count > 0) {
            glyphs[glyph_idx] = tt_load_simple_glyph(arena, scratch, font, file, (u32)contours_count);
        } else if (contours_count == -1) {
            compound_queue[queue_length] = (_CompoundQueueElement){
                .dst_idx   = glyph_idx,
                .glyph_idx = 0,
            };
            queue_length += 1;
        } else {
            // NOTE we do a trick here where we set the pointer to != 0 so we can verify we've touched this glyph
            glyphs[glyph_idx] = (TTGlyph){.contours = (void*)1}; // empty glyph (e.g. space)
        }
    }

    // TODO should this be a ring_buffer?
    u32 queue_idx = 0;
    u32 queue_end = queue_length;
    while (queue_length > 0) {
        // process compound
        // if compound refers to an untouched glyph, requeue it
    }

    return glyphs;
}
