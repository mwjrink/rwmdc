#pragma once

#include "document.h"
#include "render.h"
#include "md_context.h"
#include <math.h>
#include <stdio.h>

typedef struct LayoutStop { u32 byte; f32 x; } LayoutStop;
typedef struct LayoutLine {
    u32 begin, end, first_stop, stop_count, block;
    f32 y, height, left, right;
} LayoutLine;
typedef struct RenderWindow {
    GlyphDrawCmd *glyphs;
    u32 glyph_count;
    GpuRect *rects;
    u32 rect_count;
    u64 revision;
    u32 source_begin, source_end;
    LayoutLine *lines;
    LayoutStop *stops;
    u32 line_count, stop_count;
    u32 glyph_cap, rect_cap, line_cap, stop_cap;
} RenderWindow;
typedef struct CaretVisual { f32 x, y, width, height; bool visible; } CaretVisual;
typedef struct LayoutProfile {
    MdParserProfile parser;
    u64 index_ns, grapheme_ns, cache_ns, mirror_ns;
    u32 full_parses, local_parses;
} LayoutProfile;
typedef struct LayoutEditProfile { u64 mirror_ns, invalidation_ns; } LayoutEditProfile;
typedef struct LayoutState {
    RenderWindow window;
    f32 scroll_y, width, height;
    u32 anchor_byte;
    u64 parse_ns, layout_ns, last_parsed_bytes;
    u8 *mirror;
    u32 mirror_len, mirror_cap, trailing_begin;
    u64 document_revision;
    MdIndex index, scratch;
    MdParser parser;
    LayoutProfile profile;
    LayoutEditProfile edit_profile;
    /* Baseline records never move during local edits. Prefix offsets are
     * start-anchored; suffix offsets resolve relative to the baseline EOF. */
    u32 baseline_len, local_begin, local_end;
    bool local_ready;
    const FontState *font;
    const TTFont *font_face;
    const GlyphMetrics *font_metrics;
    f32 font_line_height, font_ascent, font_scale;
    f32 anchor_y, overscan;
    u32 local_block, reveal_byte;
    u32 affinity_byte;
    bool affinity_valid, affinity_upstream;
    bool initialized, dirty, parse_dirty, full_parse, failed, reveal_pending;
} LayoutState;

internal void layout_init(LayoutState *s) {
    *s = (LayoutState){.dirty=true, .parse_dirty=true, .full_parse=true, .local_block=UINT32_MAX};
}
internal void layout_cache_trailing_breaks(LayoutState *s) {
    u32 at = s->mirror_len;
    while (at && (s->mirror[at - 1] == '\n' || s->mirror[at - 1] == '\r')) --at;
    s->trailing_begin = at;
}
internal void layout_clear_affinity(LayoutState *s) {
    s->affinity_valid = false;
}
internal void layout_destroy(LayoutState *s) {
    free(s->mirror); md_index_destroy(&s->index); md_index_destroy(&s->scratch);
    md_parser_destroy(&s->parser);
    free(s->window.glyphs); free(s->window.rects); free(s->window.lines); free(s->window.stops);
    *s = (LayoutState){0};
}
internal const MdIndex *layout_block_index(const LayoutState *s, u32 block) {
    return block == s->local_block && s->local_ready ? &s->scratch : &s->index;
}
internal i64 layout_block_delta(const LayoutState *s, u32 block) {
    if (block == s->local_block && s->local_ready) return s->local_begin;
    return s->local_block != UINT32_MAX && block > s->local_block ?
        (i64)s->mirror_len - s->baseline_len : 0;
}
internal MdRange layout_range_offset(MdRange range, i64 delta) {
    return (MdRange){(u32)((i64)range.begin + delta), (u32)((i64)range.end + delta)};
}
internal MdBlock layout_block_view(const LayoutState *s, u32 block) {
    const MdIndex *idx = layout_block_index(s, block);
    MdBlock b = idx->blocks[idx == &s->scratch ? 0 : block];
    i64 delta = layout_block_delta(s, block);
    b.begin = (u32)((i64)b.begin + delta); b.end = (u32)((i64)b.end + delta);
    b.content = layout_range_offset(b.content, delta);
    b.open = layout_range_offset(b.open, delta); b.close = layout_range_offset(b.close, delta);
    if (block == s->local_block) { b.begin = s->local_begin; b.end = s->local_end; }
    return b;
}
internal MdRun layout_run_view(const LayoutState *s, u32 block, u32 run) {
    MdRun r = layout_block_index(s, block)->runs[run];
    i64 delta = layout_block_delta(s, block);
    r.begin = (u32)((i64)r.begin + delta); r.end = (u32)((i64)r.end + delta);
    r.block = block;
    return r;
}
internal u32 layout_block_at(const LayoutState *s, u32 byte) {
    u32 lo = 0, hi = s->index.block_count;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (layout_block_view(s, mid).end <= byte) lo = mid + 1; else hi = mid;
    }
    return min(lo, s->index.block_count ? s->index.block_count - 1 : 0);
}
internal u32 layout_snap(const LayoutState *s, const Document *doc, u32 byte) {
    if (!byte || byte >= document_length(doc)) return min(byte, document_length(doc));
    if (!s->initialized || s->parse_dirty || !s->index.point_count)
        return document_prev_grapheme(doc, document_next_grapheme(doc, byte));
    // Adjacent ASCII scalars always break, except CRLF. Avoid replaying a
    // checkpoint for the overwhelmingly common source-map boundary.
    if (s->mirror[byte - 1] < 128 && s->mirror[byte] < 128 &&
        !(s->mirror[byte - 1] == '\r' && s->mirror[byte] == '\n')) return byte;
    u32 bi = layout_block_at(s, byte);
    MdBlock block = layout_block_view(s, bi);
    const MdBlock *b = &block;
    const MdIndex *idx = layout_block_index(s, bi);
    i64 delta = layout_block_delta(s, bi);
    if (byte < b->begin) return byte; /* Hidden reference-definition prefix. */
    u32 lo = b->first_point, hi = lo + b->point_count;
    while (lo + 1 < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if ((i64)idx->points[mid] + delta <= byte) lo = mid; else hi = mid;
    }
    u32 at = (u32)((i64)idx->points[lo] + delta), boundary = at;
    i32 previous = 0, state = 0;
    bool first = true;
    while (at <= byte && at < b->end) {
        i32 cp;
        utf8proc_ssize_t n = utf8proc_iterate(s->mirror + at, b->end - at, &cp);
        if (n < 1) break;
        if (first || utf8proc_grapheme_break_stateful(previous, cp, &state)) boundary = at;
        at += (u32)n; previous = cp; first = false;
    }
    return boundary;
}
internal u32 layout_line_at(const LayoutState *s, u32 byte) {
    const RenderWindow *w = &s->window;
    u32 lo = 0, hi = w->line_count;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (w->lines[mid].end <= byte && mid + 1 < w->line_count) lo = mid + 1; else hi = mid;
    }
    return min(lo, w->line_count ? w->line_count - 1 : 0);
}
/* A wrap boundary has two visual positions but one source byte. Keep only
 * source identity and upstream/downstream affinity, never a stale line index. */
internal u32 layout_caret_line(const LayoutState *s, u32 byte) {
    u32 line = layout_line_at(s, byte);
    if (line && s->affinity_valid && s->affinity_upstream && s->affinity_byte == byte &&
        s->window.lines[line].begin == byte) {
        const LayoutLine *previous = &s->window.lines[line - 1];
        if (previous->end == byte && previous->stop_count &&
            s->window.stops[previous->first_stop + previous->stop_count - 1].byte == byte) --line;
    }
    return line;
}
internal u32 layout_set_affinity(LayoutState *s, const LayoutLine *line, u32 byte) {
    s->affinity_valid = true;
    s->affinity_byte = byte;
    s->affinity_upstream = byte == line->end && byte != line->begin;
    return byte;
}
internal u32 layout_viewport_line(const LayoutState *s) {
    for (u32 i = 0; i < s->window.line_count; ++i)
        if (s->window.lines[i].y + s->window.lines[i].height + s->scroll_y > 0) return i;
    return s->window.line_count ? s->window.line_count - 1 : 0;
}
internal u32 layout_viewport_byte(const LayoutState *s) {
    return s->window.line_count ? s->window.lines[layout_viewport_line(s)].begin : s->anchor_byte;
}
internal void layout_capture_anchor(LayoutState *s) {
    if (!s->window.line_count) return;
    LayoutLine *line = &s->window.lines[layout_viewport_line(s)];
    s->anchor_byte = line->begin; s->anchor_y = line->y + s->scroll_y;
}
internal u32 layout_shift_byte(u32 value, const DocumentEdit *e) {
    if (value < e->offset) return value;
    if (value <= e->offset + e->removed_len) return e->offset + e->inserted.len;
    return (u32)((i64)value + (i64)e->inserted.len - e->removed_len);
}
internal bool layout_plain_byte(u8 c) {
    return c == ' ' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c >= 128;
}
internal void layout_apply_edit(LayoutState *s, const Document *doc, const DocumentEdit *e) {
    u64 start = md_clock_ns();
    s->edit_profile = (LayoutEditProfile){0};
    layout_clear_affinity(s);
    if (!s->dirty) layout_capture_anchor(s);
    s->anchor_byte = layout_shift_byte(s->anchor_byte, e);
    if (s->reveal_pending) s->reveal_byte = layout_shift_byte(s->reveal_byte, e);
    s->dirty = true;
    if (!s->initialized) goto done;
    if (e->revision != s->document_revision + 1 || e->offset > s->mirror_len ||
        e->removed_len > s->mirror_len - e->offset) {
        s->initialized = false; s->parse_dirty = s->full_parse = true; goto done;
    }
    u32 block = layout_block_at(s, e->offset);
    bool safe = s->index.block_count && (!s->parse_dirty || !s->full_parse) &&
        (s->local_block == UINT32_MAX || s->local_block == block);
    MdBlock b = {0};
    if (safe) {
        b = layout_block_view(s, block);
        u32 after = e->offset + e->removed_len;
        bool interior = after < b.end && layout_plain_byte(s->mirror[after]);
        bool append = !e->removed_len && e->offset <= b.end;
        for (u32 at = e->offset; append && at < b.end; ++at)
            if (s->mirror[at] != '\r' && s->mirror[at] != '\n') append = false;
        safe = b.local_safe && e->offset > b.begin && after <= b.end &&
            layout_plain_byte(s->mirror[e->offset - 1]) && (interior || append);
        for (u32 i = 0; safe && i < e->removed_len; ++i) safe = layout_plain_byte(s->mirror[e->offset + i]);
        for (u32 i = 0; safe && i < e->inserted.len; ++i) safe = layout_plain_byte(e->inserted.data[i]);
    }
    u32 next_len = s->mirror_len - e->removed_len + e->inserted.len;
    u64 mirror_start = md_clock_ns();
    if (!md_reserve((void **)&s->mirror, &s->mirror_cap, next_len + 1, 1)) {
        s->edit_profile.mirror_ns = md_clock_ns() - mirror_start;
        s->failed = true; s->initialized = false; s->parse_dirty = s->full_parse = true; goto done;
    }
    memmove(s->mirror + e->offset + e->inserted.len, s->mirror + e->offset + e->removed_len,
        s->mirror_len - e->offset - e->removed_len);
    if (e->inserted.len) memcpy(s->mirror + e->offset, e->inserted.data, e->inserted.len);
    s->mirror[next_len] = 0; s->mirror_len = next_len; s->document_revision = e->revision;
    s->edit_profile.mirror_ns = md_clock_ns() - mirror_start;
    if (e->offset + e->removed_len >= s->trailing_begin) layout_cache_trailing_breaks(s);
    else s->trailing_begin = layout_shift_byte(s->trailing_begin, e);
    if (safe) {
        s->local_begin = b.begin;
        s->local_end = (u32)((i64)b.end + (i64)e->inserted.len - e->removed_len);
        s->local_block = block; s->full_parse = false;
    } else s->full_parse = true;
    s->parse_dirty = true;
done:
    s->edit_profile.invalidation_ns = md_clock_ns() - start - s->edit_profile.mirror_ns;
    (void)doc;
}
internal void layout_accumulate_parser(MdParserProfile *total, const MdParserProfile *part) {
    total->total_ns += part->total_ns;
    total->block_ns += part->block_ns;
    total->inline_ns += part->inline_ns;
    total->callback_ns += part->callback_ns;
    total->input_bytes += part->input_bytes;
    total->event_count += part->event_count;
    total->allocations += part->allocations;
    total->reallocations += part->reallocations;
    total->realloc_copied_bytes += part->realloc_copied_bytes;
    total->arena_growths += part->arena_growths;
    total->arena_peak_bytes = max(total->arena_peak_bytes, part->arena_peak_bytes);
    total->arena_capacity_bytes = max(total->arena_capacity_bytes, part->arena_capacity_bytes);
}
internal bool layout_parse_slice(LayoutState *s, MdIndex *idx, u32 begin, u32 length, bool local) {
    MdParserProfile profile = {0};
    if (local) ++s->profile.local_parses; else ++s->profile.full_parses;
    s->last_parsed_bytes += length;
    bool success = md_parse_index(&s->parser, idx, s->mirror + begin, length,
        &profile, &s->profile.index_ns, &s->profile.grapheme_ns);
    layout_accumulate_parser(&s->profile.parser, &profile);
    return success;
}
internal bool layout_parse(LayoutState *s) {
    u64 start = md_clock_ns();
    bool success = false;
    if (!s->full_parse && s->local_block < s->index.block_count) {
        s->local_ready = false;
        success = layout_parse_slice(s, &s->scratch, s->local_begin, s->local_end - s->local_begin, true);
        success = success && s->scratch.block_count == 1 && s->scratch.blocks[0].local_safe &&
            s->scratch.blocks[0].begin == 0 && s->scratch.blocks[0].end == s->local_end - s->local_begin;
        if (success) s->local_ready = true;
    }
    if (!success) {
        s->local_block = UINT32_MAX; s->local_ready = false; s->full_parse = true;
        success = layout_parse_slice(s, &s->index, 0, s->mirror_len, false);
        if (success) s->baseline_len = s->mirror_len;
    }
    u64 elapsed = md_clock_ns() - start;
    s->parse_ns = elapsed;
    u64 measured = s->profile.parser.total_ns + s->profile.index_ns + s->profile.grapheme_ns;
    s->profile.cache_ns += elapsed > measured ? elapsed - measured : 0;
    if (success) s->parse_dirty = false;
    return success;
}

internal u32 layout_glyph(const FontState *font, i32 cp) {
    if (cp < 32 || cp > 126) return 0;
    i32 index = font->font->glyph_map[cp];
    return index >= 0 && (u32)index < font->glyph_count ? (u32)index : 0;
}
internal bool layout_stop(RenderWindow *w, u32 byte, f32 x) {
    if (w->stop_count && w->stops[w->stop_count - 1].byte == byte &&
        w->stops[w->stop_count - 1].x == x && w->line_count &&
        w->stop_count > w->lines[w->line_count - 1].first_stop) return true;
    if (!md_reserve((void **)&w->stops, &w->stop_cap, w->stop_count + 1, sizeof(LayoutStop))) return false;
    w->stops[w->stop_count++] = (LayoutStop){byte, x}; return true;
}
internal bool layout_rect(RenderWindow *w, GpuRect rect) {
    if (!md_reserve((void **)&w->rects, &w->rect_cap, w->rect_count + 1, sizeof(GpuRect))) return false;
    w->rects[w->rect_count++] = rect; return true;
}
internal bool layout_draw(RenderWindow *w, const FontState *font, i32 cp, f32 x, f32 y, f32 scale, u32 style) {
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') return true;
    if (!md_reserve((void **)&w->glyphs, &w->glyph_cap, w->glyph_count + 1, sizeof(GlyphDrawCmd))) return false;
    u32 glyph = layout_glyph(font, cp);
    if (style & MD_STYLE_BOLD) glyph |= GLYPH_STYLE_BOLD;
    if (style & MD_STYLE_ITALIC) glyph |= GLYPH_STYLE_ITALIC;
    w->glyphs[w->glyph_count++] = (GlyphDrawCmd){.x=x, .y=y + font->font->ascent * scale,
        .sx=scale, .sy=-scale, .glyph_idx=glyph, .color=style & MD_STYLE_LINK ? 0xffffc080u : 0xffffffffu};
    return true;
}
internal bool layout_begin_line(LayoutState *s, u32 byte, u32 block, f32 y, f32 height, f32 left) {
    RenderWindow *w = &s->window;
    if (!md_reserve((void **)&w->lines, &w->line_cap, w->line_count + 1, sizeof(LayoutLine))) return false;
    w->lines[w->line_count++] = (LayoutLine){.begin=byte, .end=byte, .first_stop=w->stop_count,
        .block=block, .y=y, .height=height, .left=left, .right=left};
    return layout_stop(w, byte, left);
}
internal bool layout_end_line(LayoutState *s, u32 byte, f32 x) {
    RenderWindow *w = &s->window;
    LayoutLine *line = &w->lines[w->line_count - 1];
    bool decorated = line->stop_count != 0;
    /* Source coverage includes hidden suffixes and inter-block whitespace;
     * those bytes are not a new visual endpoint at the same x. */
    line->end = byte; line->right = x; line->stop_count = w->stop_count - line->first_stop;
    if (!decorated && line->block < s->index.block_count) {
        MdBlock block = layout_block_view(s, line->block);
        const MdBlock *b = &block;
        if (b->type == MD_BLOCK_CODE && !layout_rect(w, (GpuRect){.x=line->left - 4, .y=line->y,
            .width=max(1.0f,s->width - line->left - 8), .height=line->height, .color=0xff26221fu})) return false;
        for (u32 q = 0; q < b->quote_depth; ++q)
            if (!layout_rect(w, (GpuRect){.x=13 + q * 16.0f, .y=line->y, .width=2,
                .height=line->height, .color=0xff777777u})) return false;
    }
    return true;
}

/* Iteration starts in a retained MD4C text run, never by parsing a viewport
 * substring. Its byte budget is local even for a single multi-megabyte run. */
typedef struct LayoutUnit { u32 begin, end, style; i32 cp[2]; u32 count, repeat; bool hard; } LayoutUnit;
internal bool layout_unit(const LayoutState *s, const Document *doc, const MdRun *r, u32 at, LayoutUnit *u) {
    (void)doc;
    *u = (LayoutUnit){.begin=at, .style=r->style, .count=1, .repeat=1};
    if (r->synthetic || r->type == MD_TEXT_BR || r->type == MD_TEXT_SOFTBR) {
        /* Preserve CommonMark paragraph context while exposing source Enter
         * visually in the editor rather than folding a soft break to space. */
        if ((r->type == MD_TEXT_BR || r->type == MD_TEXT_SOFTBR) && at < r->end &&
            (s->mirror[at] == ' ' || s->mirror[at] == '\t')) {
            u->end = at + 1; u->cp[0] = s->mirror[at];
            return true;
        }
        u->end = r->end; u->cp[0] = r->type == MD_TEXT_SOFTBR ? '\n' : (i32)r->synthetic;
        u->repeat = max(1u, r->repeat);
        if (!u->cp[0]) u->cp[0] = '\n';
        u->hard = r->type == MD_TEXT_BR || r->type == MD_TEXT_SOFTBR || u->cp[0] == '\n';
        return true;
    }
    if (r->type == MD_TEXT_ENTITY && at == r->begin) {
        u->count = md_entity(s->mirror + at, r->end - at, u->cp);
        if (u->count) { u->end = r->end; return true; }
        u->count = 1;
    }
    if (at >= r->end) return false;
    u8 ascii = s->mirror[at];
    if (ascii >= 32 && ascii < 127 &&
        (at + 1 == r->end || s->mirror[at + 1] < 128)) {
        u->cp[0] = ascii;
        u->end = at + 1;
        return true;
    }
    utf8proc_ssize_t n = utf8proc_iterate(s->mirror + at, r->end - at, &u->cp[0]);
    if (n < 1) { n = 1; u->cp[0] = 0xfffd; }
    /* A full extended grapheme boundary resets the cluster context. Stream the
     * next cluster locally instead of replaying a giant Unicode source line
     * once for every visible glyph. */
    u->end = at + (u32)n;
    i32 previous = u->cp[0], state = 0;
    while (u->end < r->end) {
        i32 next;
        utf8proc_ssize_t width = utf8proc_iterate(s->mirror + u->end, r->end - u->end, &next);
        if (width < 1 || utf8proc_grapheme_break_stateful(previous, next, &state)) break;
        u->end += (u32)width; previous = next;
    }
    if (u->cp[0] == '\r' || u->cp[0] == '\n') {
        if (r->style & MD_STYLE_CODE && layout_block_view(s, r->block).type != MD_BLOCK_CODE) u->cp[0] = ' ';
        else u->hard = true;
    }
    if (!u->cp[0]) u->cp[0] = 0xfffd;
    return true;
}
internal f32 layout_unit_width(const LayoutUnit *u, const FontState *font, f32 scale, f32 x, f32 left) {
    if (u->hard) return 0;
    f32 space = max(1.0f, font->metrics[layout_glyph(font, ' ')].advance_width * scale);
    if (u->cp[0] == '\t') return space * 4 - fmodf(x - left, space * 4);
    f32 width = 0;
    for (u32 i = 0; i < u->count; ++i) {
        if (utf8proc_category(u->cp[i]) == UTF8PROC_CATEGORY_MN) continue;
        width += max(1.0f, font->metrics[layout_glyph(font, u->cp[i])].advance_width * scale);
    }
    return width * u->repeat;
}
internal bool layout_build(LayoutState *s, const Document *doc, const FontState *font, u32 begin, f32 target_bottom) {
    RenderWindow *w = &s->window;
    w->glyph_count = w->rect_count = w->line_count = w->stop_count = 0;
    w->source_begin = begin; w->source_end = begin;
    u32 requested_begin = begin;
    f32 y = begin ? 0 : 12, x = 12;
    bool anchor_seen = false, finished = true;
    u32 first = layout_block_at(s, begin);
    for (u32 bi = first; bi < s->index.block_count; ++bi) {
        MdBlock block = layout_block_view(s, bi);
        const MdBlock *b = &block;
        f32 scale = b->scale, height = max(1.0f, font->font->line_height * scale);
        f32 left = min(max(12.0f, s->width - 24), 12 + b->quote_depth * 16.0f + b->list_depth * 24.0f);
        x = left;
        u32 byte = max(begin, b->begin);
        if (!bi && !begin) byte = 0;
        if (byte > b->end) continue;
        if (s->trailing_begin < s->mirror_len && byte >= s->trailing_begin) break;
        if (!layout_begin_line(s, byte, bi, y, height, left)) return false;
        if (b->bullet && byte <= b->begin) {
            char bullet[32];
            if (b->ordered) snprintf(bullet, sizeof(bullet), "%u.", b->number); else strcpy(bullet, "-");
            f32 bx = left - (f32)strlen(bullet) * font->metrics[layout_glyph(font, '0')].advance_width * scale - 6;
            for (char *p = bullet; *p; ++p) {
                if (!layout_draw(w, font, *p, bx, y, scale, 0)) return false;
                bx += font->metrics[layout_glyph(font, *p)].advance_width * scale;
            }
        }
        if (b->type == MD_BLOCK_HR) {
            if (!layout_rect(w, (GpuRect){.x=left, .y=y + height * .5f, .width=max(1.0f,s->width - left - 12),
                .height=1, .color=0xff888888u})) return false;
        }
        bool blank = b->type == MD_BLOCK_BLANK;
        u32 ri = blank ? 0 : b->first_run, rend = blank ? 1 : ri + b->run_count;
        MdRun blank_run = {.begin=b->begin, .end=b->end, .block=bi, .type=MD_TEXT_NORMAL};
        /* Binary-search a giant paragraph's token list; a single giant run
         * begins directly at byte, without scanning its preceding contents. */
        u32 lo = ri, hi = rend;
        while (lo < hi) {
            u32 mid = lo + (hi - lo) / 2;
            MdRun run = blank ? blank_run : layout_run_view(s, bi, mid);
            const MdRun *r = &run;
            if (r->end < byte || (r->end == byte && !r->synthetic)) lo = mid + 1; else hi = mid;
        }
        ri = lo;
        for (; ri < rend; ++ri) {
            MdRun run = blank ? blank_run : layout_run_view(s, bi, ri);
            const MdRun *r = &run;
            u32 at = max(byte, r->begin);
            if (r->type == MD_TEXT_ENTITY && at < r->end) at = r->begin;
            while (at < r->end || (r->synthetic && at == r->begin)) {
                LayoutUnit unit;
                if (!layout_unit(s, doc, r, at, &unit)) break;
                /* Terminal source breaks are laid out below even when MD4C
                 * omits them; don't also create rows from code/HTML callbacks. */
                if (unit.hard && unit.begin >= s->trailing_begin) goto finish_leaf;
                u32 map_begin = unit.begin == r->begin ? layout_snap(s, doc, unit.begin) : unit.begin;
                u32 map_end = unit.end == r->end ? layout_snap(s, doc, unit.end) : unit.end;
                f32 advance = layout_unit_width(&unit, font, scale, x, left);
                if (!unit.hard && map_begin == unit.begin && x > left && x + advance > max(left + 1, s->width - 12)) {
                    if (!layout_end_line(s, map_begin, x)) return false;
                    y += height; x = left;
                    if (!layout_begin_line(s, map_begin, bi, y, height, left)) return false;
                }
                if (!anchor_seen && unit.end >= s->anchor_byte) {
                    anchor_seen = true;
                    target_bottom += y;
                }
                if (anchor_seen && y > target_bottom) { finished = false; goto done; }
                if (!layout_stop(w, map_begin, x)) return false;
                if (r->style & MD_STYLE_CODE && b->type != MD_BLOCK_CODE && advance > 0 &&
                    !layout_rect(w, (GpuRect){.x=x, .y=y, .width=advance, .height=height, .color=0xff302b27u})) return false;
                f32 dx = x;
                for (u32 ci = 0; ci < unit.count; ++ci) {
                    if (!unit.hard && !layout_draw(w, font, unit.cp[ci], dx, y, scale, r->style)) return false;
                    if (utf8proc_category(unit.cp[ci]) != UTF8PROC_CATEGORY_MN)
                        dx += font->metrics[layout_glyph(font, unit.cp[ci])].advance_width * scale;
                }
                x += advance;
                if (!unit.hard && !layout_stop(w, map_end, x)) return false;
                byte = unit.end;
                if (unit.hard) {
                    if (!layout_end_line(s, unit.end, x)) return false;
                    y += height; x = left;
                    if (!layout_begin_line(s, unit.end, bi, y, height, left)) return false;
                }
                if (unit.end <= at) break;
                at = unit.end;
            }
        }
finish_leaf:
        /* Avoid an extra empty visual line after MD4C's trailing code newline. */
        if (w->line_count > 1 && w->lines[w->line_count - 1].begin == byte && x == left &&
            w->lines[w->line_count - 2].block == bi &&
            w->lines[w->line_count - 1].first_stop + 1 == w->stop_count &&
            (b->type == MD_BLOCK_CODE || blank)) {
            --w->stop_count; --w->line_count; y -= height;
            x = w->lines[w->line_count - 1].right;
        }
        u32 covered_end = min(b->end, s->trailing_begin);
        if (!layout_end_line(s, covered_end, x)) return false;
        y += height;
        begin = b->end;
        if (!anchor_seen && covered_end >= s->anchor_byte) {
            anchor_seen = true;
            target_bottom += y;
        }
        if (anchor_seen && y > target_bottom) {
            finished = bi + 1 == s->index.block_count && s->trailing_begin == s->mirror_len;
            goto done;
        }
    }
    if (s->trailing_begin < s->mirror_len) {
        u32 at = max(requested_begin, s->trailing_begin);
        f32 height = max(1.0f, font->font->line_height);
        if (!w->line_count) {
            if (!layout_begin_line(s, at, UINT32_MAX, y, height, 12) ||
                !layout_end_line(s, at, 12)) return false;
            y += height;
        }
        while (at < s->mirror_len) {
            u32 next = at + 1;
            if (s->mirror[at] == '\r' && next < s->mirror_len && s->mirror[next] == '\n') ++next;
            /* A newline is covered by the preceding row, but its end is a
             * caret stop only on the newly created empty row. */
            w->lines[w->line_count - 1].end = next;
            if (!layout_begin_line(s, next, UINT32_MAX, y, height, 12) ||
                !layout_end_line(s, next, 12)) return false;
            if (!anchor_seen && next >= s->anchor_byte) {
                anchor_seen = true; target_bottom += y;
            }
            y += height; at = next;
            if (anchor_seen && y > target_bottom && at < s->mirror_len) {
                finished = false; goto done;
            }
        }
    }
    if (!w->line_count) {
        if (!layout_begin_line(s, 0, UINT32_MAX, 12, max(1.0f,font->font->line_height), 12)) return false;
        if (!layout_end_line(s, s->mirror_len, 12)) return false;
    }
done:
    if (w->line_count) {
        LayoutLine *last = &w->lines[w->line_count - 1];
        if (!last->stop_count && !layout_end_line(s, last->begin, last->left)) return false;
        w->source_begin = w->lines[0].begin;
        w->source_end = finished ? s->mirror_len : w->lines[w->line_count - 1].end;
    }
    return true;
}

internal CaretVisual layout_caret(const LayoutState *s, u32 byte) {
    const RenderWindow *w = &s->window;
    if (!w->line_count || byte < w->source_begin || byte > w->source_end) return (CaretVisual){0};
    const LayoutLine *line = &w->lines[layout_caret_line(s, byte)];
    f32 x = line->left;
    for (u32 i = 0; i < line->stop_count; ++i) {
        const LayoutStop *stop = &w->stops[line->first_stop + i];
        x = stop->x;
        if (stop->byte >= byte) break;
    }
    return (CaretVisual){x, line->y, 1.5f, line->height,
        line->y + line->height + s->scroll_y > 0 && line->y + s->scroll_y < s->height};
}
internal u32 layout_nearest_stop(const RenderWindow *w, const LayoutLine *line, f32 x) {
    u32 byte = line->begin;
    f32 distance = INFINITY;
    for (u32 i = 0; i < line->stop_count; ++i) {
        const LayoutStop *stop = &w->stops[line->first_stop + i];
        f32 d = fabsf(stop->x - x);
        /* At a hidden prefix prefer the actual visible text's source offset. */
        if (d <= distance) { distance = d; byte = stop->byte; }
    }
    return byte;
}
internal u32 layout_hit_test(LayoutState *s, f32 viewport_x, f32 viewport_y) {
    const RenderWindow *w = &s->window;
    if (!w->line_count) return 0;
    f32 y = viewport_y - s->scroll_y;
    u32 li = w->line_count - 1;
    for (u32 i = 0; i < w->line_count; ++i)
        if (y < w->lines[i].y + w->lines[i].height) { li = i; break; }
    return layout_set_affinity(s, &w->lines[li], layout_nearest_stop(w, &w->lines[li], viewport_x));
}
internal u32 layout_move_vertical(LayoutState *s, u32 byte, i32 line_delta, f32 preferred_x) {
    if (!s->window.line_count) return byte;
    u32 li = layout_caret_line(s, byte);
    i64 target = (i64)li + line_delta;
    target = max(0, min(target, (i64)s->window.line_count - 1));
    if (!isfinite(preferred_x)) preferred_x = layout_caret(s, byte).x;
    const LayoutLine *line = &s->window.lines[target];
    return layout_set_affinity(s, line, layout_nearest_stop(&s->window, line, preferred_x));
}
internal u32 layout_line_edge(LayoutState *s, u32 byte, bool end) {
    if (!s->window.line_count) return byte;
    const LayoutLine *line = &s->window.lines[layout_caret_line(s, byte)];
    u32 result = end ? s->window.stops[line->first_stop + line->stop_count - 1].byte :
        layout_nearest_stop(&s->window, line, line->left);
    return layout_set_affinity(s, line, result);
}
internal u32 layout_selection_rects(const LayoutState *s, u32 begin, u32 end, GpuRect *out, u32 capacity) {
    if (begin > end) { u32 swap = begin; begin = end; end = swap; }
    if (begin == end) return 0;
    u32 count = 0;
    for (u32 i = 0; i < s->window.line_count; ++i) {
        const LayoutLine *line = &s->window.lines[i];
        if (end <= line->begin || begin >= line->end) continue;
        f32 left = line->left, right = line->right;
        for (u32 j = 0; j < line->stop_count; ++j) {
            const LayoutStop *stop = &s->window.stops[line->first_stop + j];
            if (stop->byte <= begin) left = stop->x;
            if (stop->byte < end) right = stop->x;
            else { right = stop->x; break; }
        }
        if (end > line->end) right = max(right, line->right + 4);
        if (count < capacity && out) out[count] = (GpuRect){.x=left, .y=line->y,
            .width=max(2.0f,right-left), .height=line->height, .color=0x805a87c8u};
        ++count;
    }
    return count;
}
internal void layout_clamp_scroll(LayoutState *s) {
    RenderWindow *w = &s->window;
    if (!w->line_count) { s->scroll_y = 0; return; }
    if (!w->source_begin) s->scroll_y = min(0, s->scroll_y);
    if (w->source_end == s->mirror_len) {
        LayoutLine *last = &w->lines[w->line_count - 1];
        f32 bottom = last->y + last->height + 12;
        s->scroll_y = max(s->scroll_y, s->height - bottom);
        if (!w->source_begin) s->scroll_y = min(0, s->scroll_y);
    }
}
internal void layout_scroll(LayoutState *s, const Document *doc, f32 delta_y) {
    if (!isfinite(delta_y)) return;
    s->scroll_y -= delta_y;
    layout_clamp_scroll(s);
    layout_capture_anchor(s);
    RenderWindow *w = &s->window;
    if (!w->line_count) { s->dirty = true; return; }
    /* Very large wheel/page deltas are approximate byte seeks, rather than a
     * request to lay out all intervening lines. Ordinary overscan movement
     * retains both the source anchor and glyph revision. */
    if (fabsf(delta_y) > max(96.0f, s->height * 2)) {
        f32 measured = max(1.0f, w->lines[w->line_count - 1].y - w->lines[0].y);
        f64 density = (f64)(w->source_end - w->source_begin) / measured;
        i64 byte = (i64)s->anchor_byte + (i64)((f64)delta_y * density);
        s->anchor_byte = (u32)max(0, min(byte, (i64)document_length(doc)));
        if (s->anchor_byte && s->anchor_byte < document_length(doc))
            s->anchor_byte = layout_snap(s, doc, s->anchor_byte);
        s->anchor_y = 0; s->dirty = true;
    }
    LayoutLine *last = &w->lines[w->line_count - 1];
    if ((w->source_begin && w->lines[0].y + s->scroll_y > 0) ||
        (w->source_end < document_length(doc) && last->y + last->height + s->scroll_y < s->height)) s->dirty = true;
}
internal void layout_seek(LayoutState *s, const Document *doc, u32 byte) {
    byte = min(byte, document_length(doc));
    byte = layout_snap(s, doc, byte);
    s->anchor_byte = byte; s->anchor_y = 0; s->dirty = true;
}
internal void layout_reveal(LayoutState *s, const Document *doc, u32 byte) {
    byte = min(byte, document_length(doc));
    if (s->dirty || byte < s->window.source_begin || byte > s->window.source_end) {
        s->reveal_byte = byte; s->reveal_pending = true;
        if (byte < s->window.source_begin || byte > s->window.source_end) layout_seek(s, doc, byte);
        return;
    }
    CaretVisual c = layout_caret(s, byte);
    if (c.y + s->scroll_y < 12) s->scroll_y = 12 - c.y;
    else if (c.y + c.height + s->scroll_y > s->height - 12) s->scroll_y = s->height - 12 - c.y - c.height;
    layout_scroll(s, doc, 0);
}
internal bool layout_update(LayoutState *s, const Document *doc, const FontState *font, f32 width, f32 height) {
    s->parse_ns = s->layout_ns = s->last_parsed_bytes = 0;
    s->profile = (LayoutProfile){0};
    if (!font || !font->font || !font->metrics || !font->glyph_count ||
        !isfinite(width) || !isfinite(height) || width <= 0 || height <= 0) return false;
    if (s->initialized && s->document_revision != doc->revision) {
        s->initialized = false; s->parse_dirty = s->full_parse = s->dirty = true;
    }
    if (!s->initialized) {
        u64 mirror_start = md_clock_ns();
        u32 len = document_length(doc);
        if (!md_reserve((void **)&s->mirror, &s->mirror_cap, len + 1, 1)) { s->failed = true; return false; }
        document_read(doc, 0, len, s->mirror); s->mirror[len] = 0;
        s->mirror_len = len; s->document_revision = doc->revision;
        layout_cache_trailing_breaks(s);
        s->initialized = true; s->parse_dirty = s->full_parse = s->dirty = true;
        s->profile.mirror_ns = md_clock_ns() - mirror_start;
    }
    if (s->width != width || s->height != height || s->font != font ||
        s->font_face != font->font || s->font_metrics != font->metrics ||
        s->font_line_height != font->font->line_height || s->font_ascent != font->font->ascent ||
        s->font_scale != font->font->scale) {
        if (!s->dirty) layout_capture_anchor(s);
        s->dirty = true;
    }
    s->width = width; s->height = height; s->font = font;
    s->font_face = font->font; s->font_metrics = font->metrics;
    s->font_line_height = font->font->line_height; s->font_ascent = font->font->ascent; s->font_scale = font->font->scale;
    s->overscan = max(96.0f, height * .6f);
    if (!s->dirty) return false;
    if (s->parse_dirty && !layout_parse(s)) { s->failed = true; return false; }
    u64 start = md_clock_ns();
    s->anchor_byte = min(s->anchor_byte, s->mirror_len);
    f32 advance = max(1.0f,font->metrics[layout_glyph(font, 'M')].advance_width);
    f32 line_height = max(1.0f,font->font->line_height);
    u32 budget = (u32)min(1048576.0f, max(256.0f, (width / advance + 1) * (s->overscan / line_height + 3)));
    u32 begin = s->anchor_byte > budget ? s->anchor_byte - budget : 0;
    /* Prefer an existing wrap boundary during ordinary scrolling/reflow. */
    if (s->window.line_count && !s->parse_ns && begin >= s->window.source_begin && begin <= s->window.source_end)
        begin = s->window.lines[layout_line_at(s, begin)].begin;
    else if (begin) begin = layout_snap(s, doc, begin);
    if (s->index.block_count) {
        MdBlock block = layout_block_view(s, layout_block_at(s, begin));
        const MdBlock *b = &block;
        if (begin - min(begin, b->begin) < budget / 2) begin = b->begin;
    }
    if (s->index.block_count) {
        MdBlock anchor = layout_block_view(s, layout_block_at(s, s->anchor_byte));
        if (anchor.type == MD_BLOCK_BLANK && s->anchor_byte > anchor.begin) {
            u32 at = s->anchor_byte;
            u32 rows = (u32)ceilf((height + s->overscan) / line_height) + 2;
            u32 lower = max(anchor.begin, at > budget ? at - budget : 0);
            while (at > lower && rows) {
                --at;
                if (s->mirror[at] == '\n' || s->mirror[at] == '\r') {
                    if (s->mirror[at] == '\n' && at > lower && s->mirror[at - 1] == '\r') --at;
                    --rows;
                }
            }
            if (!rows) {
                if (s->mirror[at] == '\r' && at + 1 < s->mirror_len && s->mirror[at + 1] == '\n') ++at;
                begin = at + 1;
            } else begin = at;
        }
    }
    if (s->trailing_begin < s->mirror_len && s->anchor_byte >= s->trailing_begin) {
        /* Blank rows have one source break per visual line, not a prose line's
         * worth of bytes. Bound deep seeks into arbitrarily long blank tails. */
        u32 at = s->anchor_byte;
        u32 rows = (u32)ceilf((height + s->overscan) / line_height) + 2;
        while (rows && at > s->trailing_begin) {
            --at;
            if (s->mirror[at] == '\n' && at > s->trailing_begin && s->mirror[at - 1] == '\r') --at;
            --rows;
        }
        if (!rows) begin = at;
    }
    /* Build until the anchor plus visible+overscan lines are present. The first
     * pass may start on very wide UTF-8 graphemes; retry only its local prefix
     * if EOF leaves the viewport underfilled. No global height is computed. */
    for (;;) {
        f32 bottom = height + 2 * s->overscan + fabsf(s->anchor_y);
        if (!layout_build(s, doc, font, begin, bottom)) { s->failed = true; return false; }
        RenderWindow *w = &s->window;
        u32 li = layout_line_at(s, s->anchor_byte);
        s->scroll_y = s->anchor_y - w->lines[li].y;
        if (!s->anchor_byte && !begin) s->scroll_y = 0;
        LayoutLine *last = &w->lines[w->line_count - 1];
        if (w->source_end == s->mirror_len && begin && last->y + last->height < height + s->overscan) {
            u32 previous = begin;
            begin = begin > budget ? begin - budget : 0;
            if (begin) begin = layout_snap(s, doc, begin);
            if (begin < previous) { budget = min(1048576u, budget * 2); continue; }
        }
        layout_clamp_scroll(s); break;
    }
    s->dirty = false; s->failed = false; ++s->window.revision;
    if (s->reveal_pending) {
        u32 byte = s->reveal_byte; s->reveal_pending = false;
        layout_reveal(s, doc, byte);
    }
    layout_capture_anchor(s);
    s->layout_ns = md_clock_ns() - start;
    return true;
}
