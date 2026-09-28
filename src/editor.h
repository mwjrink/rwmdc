#pragma once

#include <utf8proc.h>

#include <document.h>
#include <lib/grim/bp.h>
#include <lib/grim/gfx/types.h>
#include <lib/grim/math.h>
#include <lib/grim/mem/str.h>
#include <lib/grim/text/font.h>
#include <lib/md/md_interface.h>

typedef enum EditorMode {
    INSERT = 0,
    NORMAL = 1,
    VISUAL = 2,
    FIND   = 16,
} EditorMode;

// One shaped glyph: identity + scaled position/advance.
typedef struct {
    u32 id;
    f32 advance;
    f32 offset_x;
    f32 offset_y;
} ShapedGlyph;

typedef struct ListMarkerCache {
    ShapedGlyph bullet_glyphs[1];
    u32         bullet_count;

    ShapedGlyph task_unchecked_glyphs[3];
    u32         task_unchecked_count;

    ShapedGlyph task_checked_glyphs[3];
    u32         task_checked_count;
} ListMarkerCache;

typedef struct Editor {
    Document    document;
    DocumentWal wal;
    MdState*    md; // TODO not a pointer.

    ListMarkerCache markers;

    Vec2 scroll;

    Str        path;
    EditorMode mode;
    u32        selection_anchor;

    f32 space_advance; // space glyph advance in font units (scaled at use)

    // TODO background save thread + sync state
} Editor;

// typedef struct EditorUiState {
//     f32        cursor_percent;
//     Str        path;
//     EditorMode mode;
//     u32        dirty;
// } EditorUiState;

internal u32 font_shape_run(rop(rw TTFont) font,
                            rop(ro i32) codepoints,
                            u32 codepoint_count,
                            rop(rw ShapedGlyph) glyphs) {
    fontshaper_ResetGlyphStorage(&font->glyph_storage);
    for (u32 index = 0; index < codepoint_count; index++)
        fontshaper_PushGlyph(&font->glyph_storage, &font->shaper, codepoints[index], NULL, (int)index);

    fontshaper_glyph_iterator iterator;
    fontshaper_shape_error    shape_error =
        fontshaper_ShapeDirect(font->shape_scratchpad, &font->glyph_storage, FONTSHAPER_DIRECTION_LTR, &iterator);
    assert(SCOPE_EDITOR, shape_error == FONTSHAPER_SHAPE_ERROR_NONE);

    u32 count = 0;

    fontshaper_glyph* glyph;
    while (fontshaper_GlyphIteratorNext(&iterator, &glyph)) {
        ShapedGlyph* out = &glyphs[count++];

        out->id       = glyph->Id;
        out->advance  = (f32)glyph->AdvanceX;
        out->offset_x = (f32)glyph->OffsetX;
        out->offset_y = (f32)glyph->OffsetY;
    }

    return count;
}

internal Editor editor_create(rop(rw Arena) arena, rop(rw TTFont) font, ro AppSettings settings, Str path) {
    Editor editor = {0};

    // Create owned copy of path
    if (path.len) {
        editor.path.data            = arena_alloc_aligned(arena, char, path.len + 1);
        editor.path.len             = path.len;
        editor.path.null_terminated = true;
        memory_copy(editor.path.data, path.data, path.len);
        editor.path.data[path.len] = 0;
    }

    editor.document = document_load(arena, settings, editor.path, 0);

    // Eagerly shape the fixed list markers once.
    {
        i32 bullet[1]               = {0x2022};
        editor.markers.bullet_count = font_shape_run(font, bullet, 1, editor.markers.bullet_glyphs);

        i32 task_unchecked[3] = {'[', ' ', ']'};
        editor.markers.task_unchecked_count =
            font_shape_run(font, task_unchecked, 3, editor.markers.task_unchecked_glyphs);

        i32 task_checked[3]               = {'[', 'x', ']'};
        editor.markers.task_checked_count = font_shape_run(font, task_checked, 3, editor.markers.task_checked_glyphs);

        i32         space[1] = {' '};
        ShapedGlyph space_glyph[1];
        font_shape_run(font, space, 1, space_glyph);
        editor.space_advance = space_glyph[0].advance;
    }

    return editor;
}

void editor_destroy(rop(rw Editor) editor) {
    document_close(&editor->document);
}

// TODO figure out InputEvent and where to put it
typedef struct {
} InputEvent;
void editor_handle_input(rop(rw Editor) editor, InputEvent event); // TODO define InputEvent
// {
//     MdPiece pieces[3];
//     MdView  view;
//     {
//         const Document* doc   = &editor->document;
//         u32             count = 0;
//         if (doc->before_len)
//             pieces[count++] = (MdPiece){doc->before, doc->before_len};
//         if (doc->active_len)
//             pieces[count++] = (MdPiece){doc->active, doc->active_len};
//         if (doc->after_len)
//             pieces[count++] = (MdPiece){AFTER_WRITE_PTR(doc), doc->after_len};
//         view = (MdView){.pieces = pieces, .count = count};
//     }
// }

void editor_undo(rop(rw Editor) editor);
void editor_redo(rop(rw Editor) editor);

// for rendering ui: status line ui at the bottom.
DrawLayer editor_get_ui_layer(rop(rw Editor) editor);

// ---- internal helpers (skeleton) -------------------------------------------

#define MARGIN 12.0f

// Style bits parallel MD_STYLE_* (bold/italic/code/link), plus a placeholder
// for link color. Keep these in sync with how glyph style/color is packed.
enum {
    STYLE_BOLD   = 1,
    STYLE_ITALIC = 2,
    STYLE_CODE   = 4,
    STYLE_LINK   = 8,
};

internal force_inline(f32) output_glyphs_reverse(
    rop(rw DrawLayer) layer, rop(ro ShapedGlyph) glyphs, u32 glyphs_count, f32 scale, f32 x, f32 y, u32 fg_color) {
    for (u32 idx = glyphs_count; idx > 0; idx--) {
        const ShapedGlyph* glyph = &glyphs[idx - 1];
        x -= glyph->advance * scale;
        layer->glyphs[layer->glyph_count] = (GlyphDrawCmd){
            .x         = x + glyph->offset_x * scale,
            .y         = y + glyph->offset_y * scale,
            .sx        = scale,
            .sy        = -scale,
            .glyph_idx = glyph->id,
            .color     = fg_color,
        };
        layer->glyph_count += 1;
    }
    return x;
}

internal force_inline(f32) output_glyphs(
    rop(rw DrawLayer) layer, rop(ro ShapedGlyph) glyphs, u32 glyphs_count, f32 scale, f32 x, f32 y, u32 fg_color) {
    for (u32 idx = 0; idx < glyphs_count; idx++) {
        const ShapedGlyph* glyph = &glyphs[idx];
        x += glyph->advance * scale;
        layer->glyphs[layer->glyph_count] = (GlyphDrawCmd){
            .x         = x + glyph->offset_x * scale,
            .y         = y + glyph->offset_y * scale,
            .sx        = scale,
            .sy        = -scale,
            .glyph_idx = glyph->id,
            .color     = fg_color,
        };
        layer->glyph_count += 1;
    }
    return x;
}

// Compute everything render needs for this frame: visible byte range from
// scroll, md nodes, then shape/layout them into glyphs + rects.
//
// Pseudo-code / steps:
DrawLayer editor_get_text_layer(rop(rw Editor) editor,
                                rop(rw Arena) arena,
                                rop(rw ScratchArena) scratch,
                                rop(ro AppSettings) settings,
                                rop(ro Theme) theme,
                                rop(rw TTFont) font,
                                u32 width,
                                u32 height) {

    // 1. Anchor at the byte under scroll.y (linear estimate for now), then
    //    walk md blocks backward and forward until the viewport is covered.
    DocumentStats doc_stats = document_get_stats(&editor->document);

    // Initial conservative window estimate
    u32 anchor = (u32)clamp(0u, (u32)(-editor->scroll.y / font->line_height), doc_stats.length);
    u32 begin  = anchor;
    u32 end    = anchor;

    const f32 base_scale = settings->font.size / (f32)font->upem;

    // 2. Expand [begin, end) to a conservative window around the anchor.
    //    Exact viewport coverage is determined during the measure/layout pass
    //    below (it needs per-line heights); here we just over-grab bytes.
    {
        // Rough bytes-per-line estimate (anchored by the space advance).
        f32 average_advance = editor->space_advance * base_scale;
        u32 bytes_per_line  = (u32)((f32)width / average_advance) + 1;
        // Lines above/below the viewport, plus overscan slack.
        u32 lines           = (u32)((f32)height / font->line_height) + 8;
        u32 window_bytes    = bytes_per_line * lines;

        begin = anchor > window_bytes ? anchor - window_bytes : 0;
        end   = min(anchor + window_bytes, doc_stats.length);
    }

    // 3. Ask md for nodes overlapping [begin, end).
    MdResult md_nodes = md_get(editor->md, arena, scratch, begin, end);

    DrawLayer layer = {0};
    layer.glyphs    = arena_alloc_aligned(arena, GlyphDrawCmd, doc_stats.length ? doc_stats.length : 1);
    layer.rects     = arena_alloc_aligned(arena, GpuRect, md_nodes.count ? md_nodes.count : 1);

    // 4. One span per iteration, driven by an explicit stack.
    {
        struct StackEntry {
            u32 node_idx;
            u32 style_mask;
        };
        struct StackEntry* stack     = scratch_alloc_aligned(scratch, struct StackEntry, max(md_nodes.count * 2, 1));
        u32                stack_len = 0;

        // Pending background/gutter rects, closed when their block is popped.
        struct PendingRect {
            u32 node_idx;
            f32 start_y;
            f32 x;
            f32 width;
            u32 color;
        };
        struct PendingRect* pending = scratch_alloc_aligned(scratch, struct PendingRect, max(md_nodes.count * 2, 1));
        u32                 pending_len = 0;

        f32 x      = MARGIN;
        f32 y      = MARGIN;
        f32 indent = MARGIN;

        u32 i          = 0;
        u32 span_start = begin;

        u32 last_processed = 0;
        u32 style          = 0;

        while (true) {
            u32 cur_idx;

            if (stack_len > 0) {
                cur_idx = stack[stack_len - 1].node_idx;
            } else if (i < md_nodes.count) {
                stack[stack_len] = (struct StackEntry){.node_idx = i, .style_mask = style}; // push always
                stack_len += 1;
                cur_idx = i;
            } else {
                break;
            }

            last_processed = max(last_processed, cur_idx);
            MdNode cur     = md_nodes.nodes[cur_idx];

            u32 next     = i + 1;
            u32 span_end = cur.end_idx;
            if (likely(next < md_nodes.count)) { // TODO is there a way to get rid of the if statement entirely?
                                                 // make md_nodes one too long? or end loop at count - 1?
                span_end = min(cur.end_idx, md_nodes.nodes[next].start_idx);

                stack[stack_len] = (struct StackEntry){.node_idx = next, .style_mask = style};
                stack_len += 1;
            }

            f32 scale      = base_scale;
            u32 text_color = theme->font_fg;

            switch (cur.kind) {
                case MD_NODE_HIDDEN:
                    goto span_done;
                case MD_NODE_HARD_BREAK:
                    y += font->line_height;
                    x = indent;
                    goto span_done;
                case MD_NODE_SOFT_BREAK:
                    x += editor->space_advance * scale;
                    goto span_done;
                case MD_NODE_BLANK:
                    y += font->line_height * (f32)cur.data.line_count;
                    goto span_done;
                case MD_NODE_THEMATIC_BREAK:
                    layer.rects[layer.rect_count++] = (GpuRect){.x      = indent,
                                                                .y      = y,
                                                                .width  = (f32)width - indent - MARGIN,
                                                                .height = 1.0f,
                                                                .color  = theme->thematic_break_fg,
                                                                .flags  = 0};
                    y += font->line_height;
                    goto span_done;
                case MD_NODE_QUOTE:
                    indent = MARGIN + (f32)cur.data.quote_depth * 16.0f;
                    break;
                case MD_NODE_LIST:
                    indent = MARGIN + (f32)cur.data.list.depth * 24.0f;
                    break;
                case MD_NODE_LIST_ITEM: {
                    indent = MARGIN + (f32)cur.data.item.depth * 24.0f;

                    if (cur.data.item.task) {
                        u32          glyphs_count = 0;
                        ShapedGlyph* glyphs;
                        if (cur.data.item.checked) {
                            glyphs       = editor->markers.task_checked_glyphs;
                            glyphs_count = editor->markers.task_checked_count;
                        } else {
                            glyphs       = editor->markers.task_unchecked_glyphs;
                            glyphs_count = editor->markers.task_unchecked_count;
                        }

                        f32 marker_x = indent - 6.0f;
                        output_glyphs_reverse(&layer, glyphs, glyphs_count, scale, marker_x, y, theme->list_marker_fg);
                    } else if (!cur.data.item.ordered) {
                        u32          glyphs_count = editor->markers.bullet_count;
                        ShapedGlyph* glyphs       = editor->markers.bullet_glyphs;

                        f32 marker_x = indent - 6.0f;
                        output_glyphs_reverse(&layer, glyphs, glyphs_count, scale, marker_x, y, theme->list_marker_fg);
                    } else {
                        // Ordered "N." number varies; shape fresh.
                        i32 marker_codepoints[16];
                        u32 marker_codepoint_count = 0;
                        {
                            u32  number = cur.data.item.number;
                            char digits[8];
                            u32  digit_count = 0;
                            do {
                                digits[digit_count++] = (char)('0' + number % 10);
                                number /= 10;
                            } while (number && digit_count < 7);
                            while (digit_count)
                                marker_codepoints[marker_codepoint_count++] = digits[--digit_count];
                            marker_codepoints[marker_codepoint_count++] = '.';
                        }

                        ShapedGlyph glyphs[16];
                        u32 glyphs_count = font_shape_run(font, marker_codepoints, marker_codepoint_count, glyphs);

                        f32 marker_x = indent - 6.0f;
                        output_glyphs_reverse(&layer, glyphs, glyphs_count, scale, marker_x, y, theme->list_marker_fg);
                    }

                    break;
                }
                case MD_NODE_CODE_BLOCK: {
                    pending[pending_len++] = (struct PendingRect){
                        .node_idx = cur_idx,
                        .start_y  = y,
                        .x        = indent - 4.0f,
                        .width    = (f32)width - indent - 8.0f,
                        .color    = theme->code_block_bg,
                    };
                    indent += 4.0f; // indentation inside a code block
                    text_color = theme->code_block_fg;
                    break;
                }
                case MD_NODE_HEADING:
                    scale *= 1.0f + 2.0f / (f32)cur.data.heading_level;
                    switch (cur.data.heading_level) {
                        case 1:
                            text_color = theme->h1_fg;
                            break;
                        case 2:
                            text_color = theme->h2_fg;
                            break;
                        case 3:
                            text_color = theme->h3_fg;
                            break;
                        case 4:
                            text_color = theme->h4_fg;
                            break;
                        case 5:
                            text_color = theme->h5_fg;
                            break;
                        default:
                            text_color = theme->h6_fg;
                            break;
                    }
                    break;
                case MD_NODE_REPLACEMENT: {
                    // Emit data.replacement.text[] (UTF-8) `repeat` times inline.
                    i32 replacement_codepoints[6];
                    i64 replacement_codepoint_count = utf8proc_decompose(cur.data.replacement.text,
                                                                         cur.data.replacement.length,
                                                                         replacement_codepoints,
                                                                         6,
                                                                         UTF8PROC_STABLE);

                    ShapedGlyph glyphs[6];
                    u32         glyphs_count =
                        font_shape_run(font, replacement_codepoints, (u32)replacement_codepoint_count, glyphs);

                    for (u32 repeat_index = 0; repeat_index < cur.data.replacement.repeat; repeat_index++) {
                        x += output_glyphs(&layer, glyphs, glyphs_count, scale, x, y, text_color);
                    }
                    goto span_done;
                }
                case MD_NODE_EMPHASIS:
                    style |= STYLE_ITALIC;
                    text_color = theme->emphasis_fg;
                    break;
                case MD_NODE_STRONG:
                    style |= STYLE_BOLD;
                    text_color = theme->strong_fg;
                    break;
                case MD_NODE_CODE_INLINE:
                    style |= STYLE_CODE;
                    text_color = theme->code_fg;
                    break;
                case MD_NODE_LINK:
                case MD_NODE_WIKI_LINK:
                    style |= STYLE_LINK;
                    text_color = theme->link_fg;
                    break;
                case MD_NODE_IMAGE:
                    text_color = theme->image_fg;
                    break;
                case MD_NODE_HTML_INLINE:
                case MD_NODE_HTML_BLOCK:
                    text_color = theme->html_fg;
                    break;
                case MD_NODE_STRIKETHROUGH:
                    text_color = theme->strikethrough_fg;
                    break;
                case MD_NODE_INSERT:
                    text_color = theme->insert_fg;
                    break;
                case MD_NODE_UNDERLINE:
                    text_color = theme->underline_fg;
                    break;
                case MD_NODE_SPOILER:
                    text_color = theme->spoiler_fg;
                    break;
                case MD_NODE_SUPERSCRIPT:
                    text_color = theme->superscript_fg;
                    break;
                case MD_NODE_SUBSCRIPT:
                    text_color = theme->subscript_fg;
                    break;
                case MD_NODE_HIGHLIGHT:
                    text_color = theme->highlight_fg;
                    break;
                case MD_NODE_MATH_INLINE:
                case MD_NODE_MATH_DISPLAY:
                    text_color = theme->math_fg;
                    break;
                case MD_NODE_FOOTNOTE_REFERENCE:
                case MD_NODE_FOOTNOTE_DEFINITION:
                case MD_NODE_FOOTNOTE_SECTION:
                    text_color = theme->footnote_fg;
                    break;
                default:
                    break;
            }

            // output one span of CONTENT (strip Markdown syntax):
            {
                // u32 content_start = cur.start_idx + cur.content_offset;
                u32 content_start = max(cur.start_idx + cur.content_offset, span_start);
                u32 content_len   = min(cur.content_length, span_end - content_start);

                u64 scratch_len_before = scratch->len;
                u8* content_bytes      = scratch_alloc_aligned(scratch, u8, content_len);
                u32 bytes_read         = document_read(&editor->document, content_start, content_len, content_bytes);

                i32* codepoints = scratch_alloc_aligned(scratch, i32, bytes_read * 16);
                i64  codepoint_count =
                    utf8proc_decompose(content_bytes, bytes_read, codepoints, bytes_read * 16, UTF8PROC_STABLE);

                //   shape               codepoint[] -> glyphs (fontshaper, one run)
                {
                    fontshaper_ResetGlyphStorage(&font->glyph_storage);
                    for (u32 codepoint_index = 0; codepoint_index < codepoint_count; codepoint_index++) {
                        fontshaper_glyph* glyph = fontshaper_PushGlyph(&font->glyph_storage,
                                                                       &font->shaper,
                                                                       codepoints[codepoint_index],
                                                                       NULL,
                                                                       (int)codepoint_index);
                        assert(SCOPE_EDITOR, glyph != NULL); // TODO proper scope + graceful failure
                    }

                    fontshaper_glyph_iterator iterator;
                    fontshaper_shape_error    shape_error = fontshaper_ShapeDirect(
                        font->shape_scratchpad, &font->glyph_storage, FONTSHAPER_DIRECTION_LTR, &iterator);
                    assert(SCOPE_EDITOR, shape_error == FONTSHAPER_SHAPE_ERROR_NONE);

                    fontshaper_glyph* glyph;

                    while (fontshaper_GlyphIteratorNext(&iterator, &glyph)) {
                        f32 advance = (f32)glyph->AdvanceX * scale;

                        // wrap at width on glyph boundaries (ligatures intact)
                        if (x + advance > (f32)width - MARGIN) {
                            x = indent;
                            y += font->line_height;
                        }

                        // append GlyphDrawCmd
                        layer.glyphs[layer.glyph_count] = (GlyphDrawCmd){
                            .x         = x + (f32)glyph->OffsetX * scale,
                            .y         = y + (f32)glyph->OffsetY * scale,
                            .sx        = scale,
                            .sy        = -scale,
                            .glyph_idx = glyph->Id,
                            .color     = text_color,
                        };

                        layer.glyph_count += 1;
                        x += advance;
                    }
                }

                //   Later: if the cursor is inside the span use span, if inside
                //   content use content (for caret/caret-adjacent rendering).
                //
                // TODO decode + shape + wrap + append (single fontshaper run).

                // TODO use checkpoints for this
                memset(scratch->data + scratch_len_before, 0, scratch->len - scratch_len_before);
                scratch->len = scratch_len_before;
            }

        span_done:;

            span_start = span_end;

            if (span_end == cur.end_idx) {
                // Close this block's pending background. Rect is pushed on enter,
                // so its index in `pending` matches the node-stack depth; scan
                // from the top down and stop at the first match.
                if (pending_len > 0 && pending[pending_len - 1].node_idx == cur_idx) {
                    struct PendingRect rect = pending[pending_len - 1];
                    pending_len -= 1;
                    layer.rects[layer.rect_count] = (GpuRect){
                        .x      = rect.x,
                        .y      = rect.start_y,
                        .width  = rect.width,
                        .height = y - rect.start_y,
                        .color  = rect.color,
                        .flags  = 0,
                    };
                    layer.rect_count += 1;
                }
                style = stack[stack_len - 1].style_mask;
                stack_len -= 1;
            }

            i = max(last_processed, i + 1);
        }
    }

    // 5. caret + selection are rects too; add them here.

    return layer;
}
