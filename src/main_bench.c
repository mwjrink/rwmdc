#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <time.h>

#define RWMD_MARKDOWN_IMPLEMENTATION
#include <lib/grim/gfx/internal_graphics.h>
#include <lib/grim/markdown/layout.h>

typedef struct AppOptions {
    u32         frames, warmup, width, height, columns, rows;
    f32         font_size, font_pt;
    bool        grid, animate, fullscreen, update_every_frame, benchmark, profile_parser;
    const char* file;
} AppOptions;

typedef struct StartupStamp {
    f64 wall, cpu;
} StartupStamp;

static f64 monotonic_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (f64)t.tv_sec * 1000.0 + (f64)t.tv_nsec / 1e6;
}

static StartupStamp startup_stamp(void) {
    struct timespec t;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return (StartupStamp){monotonic_ms(), (f64)t.tv_sec * 1000.0 + (f64)t.tv_nsec / 1e6};
}

static void startup_phase(const char* name, StartupStamp before, StartupStamp after) {
    printf(
        "Startup %-22s wall %8.3f ms | process CPU %8.3f ms\n", name, after.wall - before.wall, after.cpu - before.cpu);
}

static void report_memory(const TextRenderState* trs, const Arena* arena) {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    printf("Memory: arena used %.3f MiB | process peak RSS %.3f MiB\n",
           (f64)arena->len / 1048576.0,
           (f64)usage.ru_maxrss / 1024.0);
    printf("Text Vulkan allocations: assets %.3f MiB | draw buffers %.3f MiB | staging %.3f MiB\n",
           (f64)trs->asset_bytes / 1048576.0,
           (f64)trs->draw_buffer_bytes / 1048576.0,
           (f64)trs->staging_bytes / 1048576.0);
    printf("Allocation sizes exclude swapchain/driver internals and are not physical VRAM residency.\n");
}

static int compare_ms(const void* a, const void* b) {
    f64 x = *(const f64*)a, y = *(const f64*)b;
    return (x > y) - (x < y);
}

static void report_samples(const char* label, f64* values, u32 count) {
    if (!count) {
        printf("%s: no measured frames\n", label);
        return;
    }
    f64 sum    = 0;
    u32 misses = 0;
    for (u32 i = 0; i < count; i++) {
        sum += values[i];
        misses += values[i] >= 1.0;
    }
    qsort(values, count, sizeof(*values), compare_ms);
    printf("%s (%u): mean %.4f ms | p50 %.4f | p95 %.4f | p99 %.4f | max %.4f | >=1ms %u (%.2f%%)\n",
           label,
           count,
           sum / count,
           values[(count - 1) / 2],
           values[(u32)ceil(0.95 * count) - 1],
           values[(u32)ceil(0.99 * count) - 1],
           values[count - 1],
           misses,
           100.0 * misses / count);
}

static void collect_gpu(
    const GraphicsContext* gc, VkQueryPool pool, u32 slot, u64 mask, f64 period, f64* samples, u32* count) {
    u64 ticks[2];
    check_vkresult(
        vkGetQueryPoolResults(gc->device, pool, slot * 2, 2, sizeof(ticks), ticks, sizeof(u64), VK_QUERY_RESULT_64_BIT),
        SCOPE_GFX_COMMAND_BUFFER,
        "Read completed GPU timestamps");
    samples[(*count)++] = (f64)((ticks[1] - ticks[0]) & mask) * period / 1e6;
}

static u32 layout_benchmark(const AppOptions* opt,
                            const FontState*  font,
                            FontShape*        shape,
                            GlyphDrawCmd*     draws,
                            u32               capacity,
                            u32               width,
                            u32               height) {
    const char* corpus  = "# WYSIWYG Markdown - GPU text rendering\n"
                          "The quick brown fox jumps over the lazy dog. 0123456789\n"
                          "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz\n"
                          "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~\n"
                          "**Bold** _italic_ [link](url) `code` - cached outlines\n";
    u32         n       = 0;
    f32         advance = font->metrics[font_glyph(font, 'M')].advance_width;
    if (opt->grid) {
        f32 cw = (f32)width / (f32)opt->columns, ch = (f32)height / (f32)opt->rows;
        f32 scale = min(cw * 0.9f / advance, ch * 0.9f / font->font->line_height);
        for (u32 row = 0; row < opt->rows; row++)
            for (u32 col = 0; col < opt->columns; col++) {
                u32 cp     = 33 + (row * opt->columns + col) % 94;
                draws[n++] = (GlyphDrawCmd){.x         = (f32)col * cw + (cw - advance * scale) * 0.5f,
                                            .y         = (f32)row * ch + (ch - font->font->line_height * scale) * 0.5f +
                                                         font->font->ascent * scale,
                                            .sx        = scale,
                                            .sy        = -scale,
                                            .glyph_idx = font_glyph(font, (i32)cp),
                                            .color     = 0xffffffff};
            }
        return n;
    }
    f32         y = 12 + font->font->ascent;
    const char* p = corpus;
    while (y - font->font->descent <= (f32)height - 12 && n < capacity) {
        if (!*p)
            p = corpus;
        i32 codepoints[128];
        u32 count         = 0;
        f32 nominal_width = 0;
        while (p[count] && p[count] != '\n' && count < 128) {
            f32 next = font->metrics[font_glyph(font, (u8)p[count])].advance_width;
            if (count && nominal_width + next > (f32)width - 24)
                break;
            codepoints[count] = (u8)p[count];
            ++count;
            nominal_width += next;
        }
        if (count) {
            for (;;) {
                if (!font_shape(shape, font, codepoints, count, true)) {
                    fprintf(stderr, "rwmd: benchmark shaping failed: %s\n", shape->error);
                    exit(1);
                }
                if (shape->width <= (f32)width - 24 || count == 1)
                    break;
                --count;
            }
            for (u32 i = 0; i < shape->count && n < capacity; ++i) {
                const FontShapeGlyph* g = &shape->glyphs[i];
                draws[n++]              = (GlyphDrawCmd){
                    .x = 12 + g->x, .y = y - g->y, .sx = 1, .sy = -1, .glyph_idx = g->glyph, .color = 0xffffffff};
            }
            p += count;
        }
        if (*p == '\n')
            ++p;
        y += font->font->line_height;
    }
    return n;
}

static int run_benchmark(const AppOptions* opt) {
    StartupStamp launch = startup_stamp();
    Arena        arena  = arena_create();
    RenderTarget target = window_create(&arena, opt->width, opt->height);
    if (opt->fullscreen)
        window_set_fullscreen(target.window);
    GraphicsContext gc           = graphics_context_create(&arena, &target);
    StartupStamp    device_ready = startup_stamp();
    RenderContext   rc           = render_context_create(&arena, &gc, &target);
    RenderState     rs           = create_render_state(&arena, &rc);
    StartupStamp    frame_ready  = startup_stamp();
    FontState       font         = font_load(&arena,
                                             "assets/fonts/JetBrainsMonoNerdFontMono-Regular.ttf",
                                             opt->font_pt > 0 ? opt->font_pt : opt->font_size,
                                             opt->font_pt > 0);
    StartupStamp    font_ready   = startup_stamp();
    FontShape       shape        = {0};
    TextRenderState trs;
    text_render_init(&arena, &rc, &font, &trs);
    StartupStamp  text_ready = startup_stamp();
    GlyphDrawCmd* draws      = arena_alloc_aligned(&arena, GlyphDrawCmd, trs.max_draws);
    f64*          gpu_ms     = arena_alloc_aligned(&arena, f64, opt->frames);
    f64*          cpu_ms     = arena_alloc_aligned(&arena, f64, opt->frames);
    f64*          wall_ms    = arena_alloc_aligned(&arena, f64, opt->frames);
    u32           gpu_n = 0, sample_n = 0, submitted = 0;
    u64           measured_upload = 0, warmup_upload = 0, revision = 0;
    bool          pending[16] = {0};
    assert(SCOPE_GFX_INIT, rc.frames_in_flight <= 16);
    u32 bits = gc.timestamp_bits;
    if (!bits) {
        fprintf(stderr, "Graphics queue does not support GPU timestamps\n");
        return 1;
    }
    u64                   mask = bits == 64 ? UINT64_MAX : ((UINT64_C(1) << bits) - 1);
    VkQueryPoolCreateInfo qi   = {.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                  .queryType  = VK_QUERY_TYPE_TIMESTAMP,
                                  .queryCount = 2 * rc.frames_in_flight};
    check_vkresult(vkCreateQueryPool(gc.device, &qi, NULL, &trs.timestamp_pool),
                   SCOPE_GFX_COMMAND_BUFFER,
                   "Create benchmark timestamp pool");
    u32               last_w = 0, last_h = 0, draw_count = 0;
    bool              resized         = false;
    TextPushConstants pc              = {0};
    TextFrameStyle    style           = {.text_color = {1, 1, 1, 1}};
    f64               animation_start = monotonic_ms();
    while (submitted < opt->warmup + opt->frames) {
        f64 frame_start = monotonic_ms();
        window_poll_events(target.window);
        for (u32 e = 0; e < target.window->events.count; e++) {
            WindowEvent event = target.window->events.items[e];
            if (event.type == WINDOW_KEY && event.pressed && event.key == WKEY_ESCAPE)
                target.window->request_close = true;
        }
        window_clear_events(target.window);
        if (target.window->request_close)
            break;
        if (!target.window->width || !target.window->height) {
            struct timespec delay = {.tv_nsec = 10000000};
            nanosleep(&delay, NULL);
            continue;
        }
        if (target.window->width != target.extent.width || target.window->height != target.extent.height)
            rc.render_target_resized = true;
        if (!start_frame(&arena, &rs))
            continue;
        u32 slot = (u32)(rs.frame_count % rc.frames_in_flight);
        if (pending[slot]) {
            collect_gpu(&gc, trs.timestamp_pool, slot, mask, gc.properties.limits.timestampPeriod, gpu_ms, &gpu_n);
            pending[slot] = false;
        }
        bool size_changed = last_w != target.extent.width || last_h != target.extent.height;
        f64  cpu_start    = monotonic_ms();
        if (size_changed) {
            resized |= sample_n > 0;
            last_w               = target.extent.width;
            last_h               = target.extent.height;
            style.viewport_scale = (Vec2){2.0f / (f32)last_w, 2.0f / (f32)last_h};
        }
        if (size_changed || opt->update_every_frame) {
            draw_count = layout_benchmark(opt, &font, &shape, draws, trs.max_draws, last_w, last_h);
            revision++;
        }
        if (opt->animate)
            pc.scroll_offset.y = 4.0f * (f32)sin((frame_start - animation_start) * 0.001);
        trs.timestamp_base = slot * 2;
        text_render_frame(&rs, &trs, draws, draw_count, NULL, 0, style, pc, revision);
        f64  cpu_end  = monotonic_ms();
        bool measured = submitted >= opt->warmup;
        pending[slot] = measured;
        end_frame(&arena, &rs);
        f64 frame_end = monotonic_ms();
        if (measured) {
            cpu_ms[sample_n]    = cpu_end - cpu_start;
            wall_ms[sample_n++] = frame_end - frame_start;
            measured_upload += trs.last_upload_bytes;
        } else
            warmup_upload += trs.last_upload_bytes;
        if (!submitted) {
            StartupStamp first_present = startup_stamp();
            printf("Renderer: Slug | GPU: %s | scene: %s | font height: %.3f px\n",
                   gc.properties.deviceName,
                   opt->grid ? "grid" : "text",
                   (double)(font.font->ascent - font.font->descent));
            if (opt->font_pt > 0)
                printf("Font: %.2f pt at 96 DPI, %.3f pixels/em, line height %.3f pixels\n",
                       (double)opt->font_pt,
                       (double)(font.font->scale * font.font->upem),
                       (double)font.font->line_height);
            printf("Warmup: %u | samples: %u | frames in flight: %u | present: %s\n",
                   opt->warmup,
                   opt->frames,
                   rc.frames_in_flight,
                   present_mode_to_str(target.swapchain.present_mode));
            printf("Draw updates: %s | animation: %s\n",
                   opt->update_every_frame ? "full rebuild/upload every frame" : "cached until resize",
                   opt->animate ? "push-constant subpixel scroll" : "off");
            startup_phase("window/device", launch, device_ready);
            startup_phase("frame state", device_ready, frame_ready);
            startup_phase("font parse/metrics", frame_ready, font_ready);
            startup_phase("glyphs/text pipeline", font_ready, text_ready);
            startup_phase("init -> first present", launch, first_present);
            printf("Init excludes executable loading; first present is submission, not scanout.\n");
        }
        if (size_changed) {
            printf("Surface: %ux%u pixels\n", last_w, last_h);
            fflush(stdout);
        }
        submitted++;
    }
    check_vkresult(vkDeviceWaitIdle(gc.device), SCOPE_GFX_COMMAND_QUEUE, "Drain benchmark");
    for (u32 slot = 0; slot < rc.frames_in_flight; slot++)
        if (pending[slot])
            collect_gpu(&gc, trs.timestamp_pool, slot, mask, gc.properties.limits.timestampPeriod, gpu_ms, &gpu_n);
    printf("Final workload: %u glyph draws, %ux%u pixels%s\n",
           draw_count,
           last_w,
           last_h,
           resized ? " (RESIZED during measurement; rerun at fixed extent)" : "");
    report_samples("GPU render", gpu_ms, gpu_n);
    report_samples("CPU preparation", cpu_ms, sample_n);
    report_samples("Wall frame", wall_ms, sample_n);
    printf("Draw uploads: warmup %.3f KiB | measured %.3f KiB | average %.3f KiB/measured frame\n",
           (f64)warmup_upload / 1024.0,
           (f64)measured_upload / 1024.0,
           sample_n ? (f64)measured_upload / sample_n / 1024.0 : 0.0);
    report_memory(&trs, &arena);
    printf("Glyph construction storage: %.3f MiB (released; excludes font mapping and driver allocations)\n",
           (f64)trs.construction_bytes / 1048576.0);
    printf("A finite redraw benchmark does not establish an always-<1ms or input-to-photon bound.\n");
    vkDestroyQueryPool(gc.device, trs.timestamp_pool, NULL);
    text_render_cleanup(&gc, &trs);
    cleanup_render_state(&rs);
    cleanup_render_context(&rc);
    cleanup_render_target(&gc, &target);
    cleanup_graphics_ctx(&gc);
    close_window(target.window);
    font_shape_destroy(&shape);
    font_destroy(&font);
    arena_destroy(&arena);
    return 0;
}

typedef struct Metric {
    u64 count, over_budget;
    f64 sum, maximum;
} Metric;
static void metric_add(Metric* metric, f64 ms) {
    metric->count++;
    metric->sum += ms;
    if (ms > metric->maximum)
        metric->maximum = ms;
    metric->over_budget += ms >= 1.0;
}
static void metric_report(const char* name, const Metric* metric) {
    printf("%s: %lu samples | mean %.4f ms | max %.4f ms | >=1ms %lu\n",
           name,
           metric->count,
           metric->count ? metric->sum / (f64)metric->count : 0,
           metric->maximum,
           metric->over_budget);
}
static void metric_add_ns(Metric* metric, u64 ns) {
    metric_add(metric, (f64)ns / 1e6);
}

typedef struct Editor {
    Document         document;
    LayoutState      layout;
    const FontState* font;
    GrimWindow*      window;
    const char*      path;
    u64              saved_state;
    u32              selection_anchor;
    u32              event_index;
    bool             paste_pending;
    bool             selecting, dragging_scrollbar, close_armed, title_dirty;
    f32              drag_offset, preferred_x;
    f64              last_input;
    GpuRect*         rects;
    u32              rect_capacity;
    Metric           edits, mirror, invalidation, parsing, parser_total, block_scan, inline_events;
    Metric           callbacks, index_finalization, graphemes, cache_publication, layout_time;
    MdParserProfile  parser_counters;
    u64              full_parses, local_parses, largest_parse_update;
} Editor;

static bool editor_dirty(const Editor* editor) {
    return document_state_id(&editor->document) != editor->saved_state;
}
static void editor_title(Editor* editor, const char* message) {
    char title[2048];
    snprintf(title,
             sizeof(title),
             "%s%s — rwmd%s%s",
             editor_dirty(editor) ? "*" : "",
             editor->path ? editor->path : "Untitled",
             message ? " — " : "",
             message ? message : "");
    window_set_title(editor->window, title);
}
static void editor_error(Editor* editor, const char* message) {
    fprintf(stderr, "rwmd: %s\n", message);
    editor_title(editor, message);
}
static bool editor_update_layout(Editor* editor, f32 width, f32 height) {
    bool changed = layout_update(&editor->layout, &editor->document, editor->font, max(width - 14.0f, 1.0f), height);
    if (editor->layout.failed) {
        fprintf(stderr,
                "rwmd: Markdown layout failed%s%s\n",
                editor->layout.shape.error ? ": " : "",
                editor->layout.shape.error ? editor->layout.shape.error : "");
        exit(1);
    }
    if (changed) {
        const LayoutProfile* profile = &editor->layout.profile;
        if (profile->mirror_ns)
            metric_add_ns(&editor->mirror, profile->mirror_ns);
        if (profile->full_parses || profile->local_parses) {
            metric_add_ns(&editor->parsing, editor->layout.parse_ns);
            metric_add_ns(&editor->parser_total, profile->parser.total_ns);
            metric_add_ns(&editor->block_scan, profile->parser.block_ns);
            metric_add_ns(&editor->inline_events, profile->parser.inline_ns);
            if (editor->layout.parser.profile_callbacks)
                metric_add_ns(&editor->callbacks, profile->parser.callback_ns);
            metric_add_ns(&editor->index_finalization, profile->index_ns);
            metric_add_ns(&editor->graphemes, profile->grapheme_ns);
            metric_add_ns(&editor->cache_publication, profile->cache_ns);
            editor->full_parses += profile->full_parses;
            editor->local_parses += profile->local_parses;
            editor->largest_parse_update = max(editor->largest_parse_update, profile->parser.input_bytes);
            MdParserProfile* total       = &editor->parser_counters;
            total->input_bytes += profile->parser.input_bytes;
            total->event_count += profile->parser.event_count;
            total->allocations += profile->parser.allocations;
            total->reallocations += profile->parser.reallocations;
            total->realloc_copied_bytes += profile->parser.realloc_copied_bytes;
            total->arena_growths += profile->parser.arena_growths;
            total->arena_peak_bytes     = max(total->arena_peak_bytes, profile->parser.arena_peak_bytes);
            total->arena_capacity_bytes = max(total->arena_capacity_bytes, profile->parser.arena_capacity_bytes);
        }
        metric_add(&editor->layout_time, (f64)editor->layout.layout_ns / 1e6);
    }
    return changed;
}
static void editor_changed(Editor* editor) {
    layout_apply_edit(&editor->layout, &editor->document, &editor->document.last_edit);
    metric_add_ns(&editor->mirror, editor->layout.edit_profile.mirror_ns);
    metric_add_ns(&editor->invalidation, editor->layout.edit_profile.invalidation_ns);
    editor->selection_anchor = document_cursor(&editor->document);
    layout_reveal(&editor->layout, &editor->document, editor->selection_anchor);
    editor->last_input   = monotonic_ms();
    editor->preferred_x  = NAN;
    bool was_close_armed = editor->close_armed;
    editor->close_armed  = false;
    bool dirty           = editor_dirty(editor);
    if (dirty != editor->title_dirty || was_close_armed)
        editor_title(editor, NULL);
    editor->title_dirty = dirty;
}
static bool editor_replace(Editor* editor, u32 offset, u32 removed, const u8* text, u32 length) {
    u64  before = editor->document.revision;
    f64  start  = monotonic_ms();
    bool ok     = document_replace(&editor->document, offset, removed, text, length);
    metric_add(&editor->edits, monotonic_ms() - start);
    if (!ok) {
        editor_error(editor, document_error(&editor->document));
        return false;
    }
    if (editor->document.revision != before)
        editor_changed(editor);
    return editor->document.revision != before;
}
static bool editor_insert(Editor* editor, const u8* text, u32 length) {
    u32 caret = document_cursor(&editor->document);
    u32 begin = min(caret, editor->selection_anchor), end = max(caret, editor->selection_anchor);
    return editor_replace(editor, begin, end - begin, text, length);
}
static bool editor_space(const Document* document, u32 offset) {
    if (offset >= document_length(document))
        return true;
    u8 byte;
    document_read(document, offset, 1, &byte);
    return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
}
static u32 editor_word(const Document* document, u32 cursor, bool forward) {
    u32 length = document_length(document);
    if (forward) {
        while (cursor < length && !editor_space(document, cursor))
            cursor = document_next_grapheme(document, cursor);
        while (cursor < length && editor_space(document, cursor))
            cursor = document_next_grapheme(document, cursor);
    } else {
        while (cursor) {
            u32 prev = document_prev_grapheme(document, cursor);
            if (!editor_space(document, prev))
                break;
            cursor = prev;
        }
        while (cursor) {
            u32 prev = document_prev_grapheme(document, cursor);
            if (editor_space(document, prev))
                break;
            cursor = prev;
        }
    }
    return cursor;
}
static void editor_move(Editor* editor, u32 cursor, bool extend) {
    document_set_cursor(&editor->document, cursor);
    cursor = document_cursor(&editor->document);
    if (!extend)
        editor->selection_anchor = cursor;
    layout_reveal(&editor->layout, &editor->document, cursor);
    editor->last_input = monotonic_ms();
}
static void editor_save(Editor* editor) {
    if (!editor->path) {
        editor_error(editor, "Open with a file path to save this document");
        return;
    }
    if (!document_save(&editor->document, editor->path)) {
        editor_error(editor, document_error(&editor->document));
        return;
    }
    editor->saved_state = document_state_id(&editor->document);
    editor->title_dirty = false;
    editor->close_armed = false;
    editor_title(editor, NULL);
    printf("Saved %s (%u bytes)\n", editor->path, document_length(&editor->document));
    fflush(stdout);
}
static void editor_copy(Editor* editor, bool cut) {
    u32 caret = document_cursor(&editor->document);
    u32 begin = min(caret, editor->selection_anchor), end = max(caret, editor->selection_anchor);
    if (begin == end)
        return;
    u8* bytes = malloc(end - begin);
    if (!bytes) {
        editor_error(editor, "Clipboard allocation failed");
        return;
    }
    document_read(&editor->document, begin, end - begin, bytes);
    window_clipboard_set(editor->window, bytes, end - begin);
    free(bytes);
    if (cut)
        editor_replace(editor, begin, end - begin, NULL, 0);
}
static bool editor_key(Editor* editor, const WindowEvent* event, f32 width, f32 height) {
    if (!event->pressed)
        return false;
    bool ctrl  = (event->modifiers & WINDOW_CTRL) != 0;
    bool shift = (event->modifiers & WINDOW_SHIFT) != 0;
    u32  caret = document_cursor(&editor->document), next = caret;
    if (ctrl) {
        switch (event->key) {
            case WKEY_S:
                editor_save(editor);
                return true;
            case WKEY_A:
                layout_clear_affinity(&editor->layout);
                editor->selection_anchor = 0;
                editor_move(editor, document_length(&editor->document), true);
                return true;
            case WKEY_C:
                editor_copy(editor, false);
                return true;
            case WKEY_X:
                editor_copy(editor, true);
                return true;
            case WKEY_V:
                editor->paste_pending = true;
                window_clipboard_request(editor->window);
                return false;
            case WKEY_Z:
            case WKEY_Y: {
                f64  start   = monotonic_ms();
                bool redo    = event->key == WKEY_Y || shift;
                bool changed = redo ? document_redo(&editor->document) : document_undo(&editor->document);
                metric_add(&editor->edits, monotonic_ms() - start);
                if (changed)
                    editor_changed(editor);
                return changed;
            }
            case WKEY_Q:
                editor->window->request_close = true;
                return false;
            default:
                break;
        }
    }
    switch (event->key) {
        case WKEY_ESCAPE:
            editor->selection_anchor = caret;
            editor->selecting = editor->dragging_scrollbar = false;
            return true;
        case WKEY_ENTER:
            return editor_insert(
                editor, (const u8*)(editor->document.crlf ? "\r\n" : "\n"), editor->document.crlf ? 2 : 1);
        case WKEY_TAB:
            return editor_insert(editor, (const u8*)"\t", 1);
        case WKEY_BACKSPACE:
        case WKEY_DELETE: {
            u32 begin = min(caret, editor->selection_anchor), end = max(caret, editor->selection_anchor);
            if (begin == end) {
                if (event->key == WKEY_BACKSPACE)
                    begin = ctrl ? editor_word(&editor->document, caret, false)
                                 : document_prev_grapheme(&editor->document, caret);
                else
                    end = ctrl ? editor_word(&editor->document, caret, true)
                               : document_next_grapheme(&editor->document, caret);
            }
            return begin != end && editor_replace(editor, begin, end - begin, NULL, 0);
        }
        case WKEY_LEFT:
            layout_clear_affinity(&editor->layout);
            next                = !shift && editor->selection_anchor != caret ? min(caret, editor->selection_anchor)
                                  : ctrl ? editor_word(&editor->document, caret, false)
                                         : document_prev_grapheme(&editor->document, caret);
            editor->preferred_x = NAN;
            break;
        case WKEY_RIGHT:
            layout_clear_affinity(&editor->layout);
            next                = !shift && editor->selection_anchor != caret ? max(caret, editor->selection_anchor)
                                  : ctrl ? editor_word(&editor->document, caret, true)
                                         : document_next_grapheme(&editor->document, caret);
            editor->preferred_x = NAN;
            break;
        case WKEY_HOME:
        case WKEY_END:
            if (ctrl)
                layout_clear_affinity(&editor->layout);
            if (!ctrl)
                layout_reveal(&editor->layout, &editor->document, caret);
            editor_update_layout(editor, width, height);
            next                = ctrl ? (event->key == WKEY_HOME ? 0 : document_length(&editor->document))
                                       : layout_line_edge(&editor->layout, caret, event->key == WKEY_END);
            editor->preferred_x = NAN;
            break;
        case WKEY_UP:
        case WKEY_DOWN:
        case WKEY_PAGE_UP:
        case WKEY_PAGE_DOWN: {
            layout_reveal(&editor->layout, &editor->document, caret);
            editor_update_layout(editor, width, height);
            CaretVisual position = layout_caret(&editor->layout, caret);
            if (!isfinite(editor->preferred_x))
                editor->preferred_x = position.x;
            i32 lines = (event->key == WKEY_UP || event->key == WKEY_PAGE_UP) ? -1 : 1;
            if (event->key == WKEY_PAGE_UP || event->key == WKEY_PAGE_DOWN)
                lines *= max(1, (i32)(height / editor->font->font->line_height) - 1);
            next = layout_move_vertical(&editor->layout, caret, lines, editor->preferred_x);
            break;
        }
        default:
            return false;
    }
    editor_move(editor, next, shift);
    return true;
}
static void editor_thumb(const Editor* editor, f32 height, f32* top, f32* size) {
    u32 length = document_length(&editor->document);
    u32 first  = layout_viewport_byte(&editor->layout);
    u32 last   = first;
    for (u32 i = 0; i < editor->layout.window.line_count; i++) {
        const LayoutLine* line = &editor->layout.window.lines[i];
        if (line->y + editor->layout.scroll_y >= height)
            break;
        if (line->y + line->height + editor->layout.scroll_y > 0)
            last = line->end;
    }
    u32 visible = last >= first ? last - first : 0;
    *size       = length ? clamp(24.0f, height * (f32)visible / (f32)length, height) : height;
    *top        = length ? clamp(0.0f, (height - *size) * (f32)first / (f32)length, height - *size) : 0;
    if (editor->layout.window.source_end == length && editor->layout.window.line_count) {
        const LayoutLine* last_line = &editor->layout.window.lines[editor->layout.window.line_count - 1];
        if (last_line->y + last_line->height + editor->layout.scroll_y <= height + 0.5f)
            *top = height - *size;
    }
}
static void editor_drag_thumb(Editor* editor, f32 y, f32 height) {
    f32 top, size;
    editor_thumb(editor, height, &top, &size);
    f32 fraction = height > size ? clamp(0.0f, (y - editor->drag_offset) / (height - size), 1.0f) : 0;
    u32 byte     = (u32)((f64)fraction * document_length(&editor->document));
    layout_seek(&editor->layout, &editor->document, byte);
}
static bool editor_event(Editor* editor, const WindowEvent* event, f32 width, f32 height) {
    if (event->type == WINDOW_TEXT) {
        const u8* text = window_event_text(editor->window, event);
        return editor_insert(editor, text, event->text_len);
    }
    if (event->type == WINDOW_KEY)
        return editor_key(editor, event, width, height);
    if (event->type == WINDOW_SCROLL) {
        layout_scroll(&editor->layout, &editor->document, event->dy);
        return true;
    }
    if (event->type == WINDOW_POINTER_BUTTON && event->button == 1) {
        if (!event->pressed) {
            editor->selecting = editor->dragging_scrollbar = false;
            return false;
        }
        editor_update_layout(editor, width, height);
        if (event->x >= width - 12) {
            f32 top, size;
            editor_thumb(editor, height, &top, &size);
            editor->drag_offset        = event->y >= top && event->y <= top + size ? event->y - top : size * .5f;
            editor->dragging_scrollbar = true;
            editor_drag_thumb(editor, event->y, height);
        } else {
            u32 hit = layout_hit_test(&editor->layout, event->x, event->y);
            document_set_cursor(&editor->document, hit);
            if (!(event->modifiers & WINDOW_SHIFT))
                editor->selection_anchor = document_cursor(&editor->document);
            editor->selecting   = true;
            editor->preferred_x = NAN;
            editor->last_input  = monotonic_ms();
        }
        return true;
    }
    if (event->type == WINDOW_POINTER_MOVE) {
        if (editor->dragging_scrollbar) {
            editor_drag_thumb(editor, event->y, height);
            return true;
        }
        if (editor->selecting) {
            if (event->y < 0)
                layout_scroll(&editor->layout, &editor->document, event->y);
            else if (event->y > height)
                layout_scroll(&editor->layout, &editor->document, event->y - height);
            editor_update_layout(editor, width, height);
            document_set_cursor(&editor->document, layout_hit_test(&editor->layout, event->x, event->y));
            editor->last_input = monotonic_ms();
            return true;
        }
    }
    return false;
}

static bool editor_process_events(Editor* editor, f32 width, f32 height) {
    bool          redraw = false;
    WindowEvents* queue  = &editor->window->events;
    for (;;) {
        if (editor->paste_pending) {
            // Preserve input order across asynchronous clipboard transfers:
            // later typing/save commands remain queued, but rendering continues.
            u32 index = editor->event_index;
            while (index < queue->count && queue->items[index].type != WINDOW_PASTE)
                index++;
            if (index == queue->count)
                break;
            WindowEvent completion = queue->items[index];
            if (completion.text_len)
                redraw |= editor_insert(editor, window_event_text(editor->window, &completion), completion.text_len);
            memmove(queue->items + index,
                    queue->items + index + 1,
                    (usize)(queue->count - index - 1) * sizeof(WindowEvent));
            queue->count--;
            editor->paste_pending = false;
            continue;
        }
        if (editor->event_index == queue->count) {
            window_clear_events(editor->window);
            editor->event_index = 0;
            break;
        }
        WindowEvent event = queue->items[editor->event_index++];
        if (event.type != WINDOW_PASTE)
            redraw |= editor_event(editor, &event, width, height);
    }
    return redraw;
}
static u32 editor_rectangles(Editor* editor, f32 width, f32 height) {
    u32 caret = document_cursor(&editor->document);
    u32 begin = min(caret, editor->selection_anchor), end = max(caret, editor->selection_anchor);
    u32 selection_count = begin != end ? layout_selection_rects(&editor->layout, begin, end, NULL, 0) : 0;
    u32 static_count    = editor->layout.window.rect_count;
    u64 needed          = (u64)static_count + selection_count + 2;
    if (needed > editor->rect_capacity) {
        u32 cap = editor->rect_capacity ? editor->rect_capacity : 64;
        while (cap < needed && cap < UINT32_MAX / 2)
            cap *= 2;
        if (cap < needed) {
            fprintf(stderr, "Editor rectangle capacity overflow\n");
            exit(1);
        }
        GpuRect* grown = realloc(editor->rects, (usize)cap * sizeof(*grown));
        if (!grown) {
            perror("editor rectangles");
            exit(1);
        }
        editor->rects         = grown;
        editor->rect_capacity = cap;
    }
    if (static_count)
        memcpy(editor->rects, editor->layout.window.rects, (usize)static_count * sizeof(GpuRect));
    u32 count = static_count;
    if (selection_count)
        count += layout_selection_rects(
            &editor->layout, begin, end, editor->rects + count, editor->rect_capacity - count - 2);
    f32 top, size;
    editor_thumb(editor, height, &top, &size);
    editor->rects[count++] = (GpuRect){width - 12, 0, 12, height, 0xff201b18, GPU_RECT_FIXED};
    editor->rects[count++] = (GpuRect){width - 9, top, 6, size, 0xffaaa098, GPU_RECT_FIXED};
    return count;
}

static int run_editor(const AppOptions* opt) {
    StartupStamp launch = startup_stamp();
    Editor       editor = {.path = opt->file, .preferred_x = NAN};
    const char*  welcome =
        "# rwmd\n\nA small Markdown editor rendered with Slug.\n\n"
        "Type to edit. **Bold**, *italic*, `code`, and [links](https://example.org).\n\n"
        "- Mouse: place the caret or drag to select\n- Wheel or scrollbar: scroll independently of the caret\n"
        "- Ctrl+Z / Ctrl+Shift+Z: undo / redo\n- Ctrl+C / Ctrl+X / Ctrl+V: clipboard\n"
        "- Ctrl+S: save the file opened on the command line\n\n"
        "Open a file with: `./rwmd path/to/document.md`\n";
    bool loaded;
    if (opt->file) {
        loaded = document_load(&editor.document, opt->file);
        if (!loaded && errno == ENOENT) {
            document_destroy(&editor.document);
            loaded = document_init(&editor.document, NULL, 0);
        }
    } else
        loaded = document_init(&editor.document, (const u8*)welcome, (u32)strlen(welcome));
    if (!loaded) {
        fprintf(stderr, "rwmd: %s: %s\n", opt->file ? opt->file : "document", document_error(&editor.document));
        document_destroy(&editor.document);
        return 1;
    }
    editor.saved_state          = document_state_id(&editor.document);
    StartupStamp document_ready = startup_stamp();
    Arena        arena          = arena_create();
    RenderTarget target         = window_create(&arena, opt->width, opt->height);
    editor.window               = target.window;
    editor_title(&editor, NULL);
    if (opt->fullscreen)
        window_set_fullscreen(target.window);
    GraphicsContext gc             = graphics_context_create(&arena, &target);
    RenderContext   rc             = render_context_create(&arena, &gc, &target);
    RenderState     rs             = create_render_state(&arena, &rc);
    StartupStamp    graphics_ready = startup_stamp();
    FontState       font           = font_load(&arena,
                                               "assets/fonts/JetBrainsMonoNerdFontMono-Regular.ttf",
                                               opt->font_pt > 0 ? opt->font_pt : opt->font_size,
                                               opt->font_pt > 0);
    editor.font                    = &font;
    TextRenderState trs;
    text_render_init(&arena, &rc, &font, &trs);
    layout_init(&editor.layout);
    editor.layout.parser.profile_callbacks = opt->profile_parser;
    StartupStamp          text_ready       = startup_stamp();
    VkQueryPoolCreateInfo qi               = {.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                              .queryType  = VK_QUERY_TYPE_TIMESTAMP,
                                              .queryCount = 2 * rc.frames_in_flight};
    if (gc.timestamp_bits)
        check_vkresult(vkCreateQueryPool(gc.device, &qi, NULL, &trs.timestamp_pool),
                       SCOPE_GFX_COMMAND_BUFFER,
                       "Create editor timestamps");
    u64  mask = gc.timestamp_bits == 64 ? UINT64_MAX : gc.timestamp_bits ? ((UINT64_C(1) << gc.timestamp_bits) - 1) : 0;
    bool pending[16] = {0};
    assert(SCOPE_GFX_INIT, rc.frames_in_flight <= 16);
    Metric cpu = {0}, gpu = {0}, wall = {0}, publication = {0};
    u64    uploaded = 0, presented = 0;
    bool   redraw = true, previous_blink = true, previous_focus = target.window->focused;
    editor.last_input = monotonic_ms();
    while (!opt->frames || presented < opt->frames) {
        f64 frame_start = monotonic_ms();
        window_poll_events(target.window);
        f64 work_start = monotonic_ms();
        f32 width = (f32)target.window->width, height = (f32)target.window->height;
        redraw |= editor_process_events(&editor, width, height);
        if (target.window->request_close) {
            if (editor_dirty(&editor) && !editor.close_armed) {
                target.window->request_close = false;
                editor.close_armed           = true;
                editor_error(&editor, "Unsaved changes: Ctrl+S saves; close again to discard");
            } else
                break;
        }
        if (!target.window->width || !target.window->height) {
            window_wait_events(target.window, 100);
            continue;
        }
        if (target.window->width != target.extent.width || target.window->height != target.extent.height) {
            rc.render_target_resized = true;
            redraw                   = true;
        }
        redraw |= editor_update_layout(&editor, width, height);
        f64  elapsed = monotonic_ms() - editor.last_input;
        bool blink   = ((u64)(max(elapsed, 0.0) / 500.0) & 1u) == 0;
        if (blink != previous_blink || previous_focus != target.window->focused)
            redraw = true;
        previous_blink = blink;
        previous_focus = target.window->focused;
        if (!redraw && !opt->frames) {
            u32 wait_ms = (u32)max(1.0, 500.0 - fmod(max(elapsed, 0.0), 500.0));
            window_wait_events(target.window, wait_ms);
            continue;
        }
        f64 acquire_start = monotonic_ms();
        if (!start_frame(&arena, &rs)) {
            redraw = true;
            continue;
        }
        f64 acquire_elapsed = monotonic_ms() - acquire_start;
        // The compositor may have constrained the requested dimensions.
        width               = (f32)target.extent.width;
        height              = (f32)target.extent.height;
        editor_update_layout(&editor, width, height);
        u32 slot = (u32)(rs.frame_count % rc.frames_in_flight);
        if (pending[slot]) {
            f64 value;
            u32 count = 0;
            collect_gpu(&gc, trs.timestamp_pool, slot, mask, gc.properties.limits.timestampPeriod, &value, &count);
            metric_add(&gpu, value);
            pending[slot] = false;
        }
        u32               rect_count = editor_rectangles(&editor, width, height);
        CaretVisual       caret      = layout_caret(&editor.layout, document_cursor(&editor.document));
        TextPushConstants pc    = {.scroll_offset = {0, editor.layout.scroll_y}, .cursor_position = {caret.x, caret.y}};
        TextFrameStyle    style = {.viewport_scale = {2.0f / width, 2.0f / height},
                                   .text_color     = {1, 1, 1, 1},
                                   .cursor_size    = {max(caret.width, 1.0f), caret.height},
                                   .cursor_color   = 0xffffffff,
                                   .cursor_visible = caret.visible && blink && target.window->focused &&
                                                     editor.selection_anchor == document_cursor(&editor.document)};
        trs.timestamp_base      = slot * 2;
        f64 publication_start   = monotonic_ms();
        text_render_frame(&rs,
                          &trs,
                          editor.layout.window.glyphs,
                          editor.layout.window.glyph_count,
                          editor.rects,
                          rect_count,
                          style,
                          pc,
                          editor.layout.window.revision);
        f64 work_end = monotonic_ms();
        metric_add(&publication, work_end - publication_start);
        pending[slot] = trs.timestamp_pool != VK_NULL_HANDLE;
        end_frame(&arena, &rs);
        metric_add(&cpu, work_end - work_start - acquire_elapsed);
        metric_add(&wall, monotonic_ms() - frame_start);
        uploaded += trs.last_upload_bytes;
        if (!presented) {
            printf("Editor: %s | %u bytes | GPU: %s\n",
                   opt->file ? opt->file : "Untitled",
                   document_length(&editor.document),
                   gc.properties.deviceName);
            startup_phase("load document", launch, document_ready);
            startup_phase("window/device", document_ready, graphics_ready);
            startup_phase("font/Slug", graphics_ready, text_ready);
            startup_phase("init -> first present", launch, startup_stamp());
            printf("Surface: %ux%u | input driven; caret=%u | Ctrl+S saves, Ctrl+Z undoes\n",
                   target.extent.width,
                   target.extent.height,
                   document_cursor(&editor.document));
            fflush(stdout);
        }
        presented++;
        redraw = false;
    }
    vkDeviceWaitIdle(gc.device);
    for (u32 slot = 0; slot < rc.frames_in_flight; slot++)
        if (pending[slot]) {
            f64 value;
            u32 count = 0;
            collect_gpu(&gc, trs.timestamp_pool, slot, mask, gc.properties.limits.timestampPeriod, &value, &count);
            metric_add(&gpu, value);
        }
    metric_report("Document mutation", &editor.edits);
    metric_report("Source mirror", &editor.mirror);
    metric_report("Edit invalidation/anchors", &editor.invalidation);
    metric_report("Syntax update total", &editor.parsing);
    metric_report("  Parser total (inclusive)", &editor.parser_total);
    metric_report("    Block scan/references", &editor.block_scan);
    metric_report("    Inline/events (inclusive)", &editor.inline_events);
    if (opt->profile_parser)
        metric_report("    Callbacks (nested)", &editor.callbacks);
    else
        printf("    Callback timing disabled; --profile-parser enables per-event clocks.\n");
    metric_report("  Index finalization", &editor.index_finalization);
    metric_report("  Grapheme checkpoints", &editor.graphemes);
    metric_report("  Cache selection/publication", &editor.cache_publication);
    metric_report("Window layout", &editor.layout_time);
    metric_report("GPU upload/record CPU", &publication);
    const MdParserProfile* parser = &editor.parser_counters;
    printf("Parser work: %lu full | %lu local | %lu bytes | max %lu bytes/syntax update | %lu events\n",
           editor.full_parses,
           editor.local_parses,
           parser->input_bytes,
           editor.largest_parse_update,
           parser->event_count);
    printf("Parser arena: %lu allocation requests | %lu reallocations | %.3f KiB copied | %lu chunk growths\n",
           parser->allocations,
           parser->reallocations,
           (f64)parser->realloc_copied_bytes / 1024.0,
           parser->arena_growths);
    printf("Parser scratch: peak %.3f KiB | retained %.3f KiB; reset after each parse.\n",
           (f64)parser->arena_peak_bytes / 1024.0,
           (f64)parser->arena_capacity_bytes / 1024.0);
    printf("Syntax timing samples are updates, including any local attempt plus full fallback; nested times are not "
           "additive.\n");
    metric_report("Editor CPU work", &cpu);
    metric_report("GPU render", &gpu);
    metric_report("Wall frame", &wall);
    printf("Editor frames: %lu | uploaded %.3f KiB | document %u bytes\n",
           presented,
           (f64)uploaded / 1024.0,
           document_length(&editor.document));
    report_memory(&trs, &arena);
    if (trs.timestamp_pool)
        vkDestroyQueryPool(gc.device, trs.timestamp_pool, NULL);
    text_render_cleanup(&gc, &trs);
    cleanup_render_state(&rs);
    cleanup_render_context(&rc);
    cleanup_render_target(&gc, &target);
    cleanup_graphics_ctx(&gc);
    close_window(target.window);
    layout_destroy(&editor.layout);
    document_destroy(&editor.document);
    free(editor.rects);
    font_destroy(&font);
    arena_destroy(&arena);
    return 0;
}

int main(int argc, char** argv) {
    AppOptions opt      = {.width = 1920, .height = 1080, .columns = 100, .rows = 100, .font_size = 32, .warmup = 200};
    bool       size_set = false;
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (!strcmp(arg, "--help")) {
            printf(
                "Usage: %s [file.md] [--file PATH] [--font-pt N | --font-size N]\n"
                "  [--width N] [--height N] [--fullscreen] [--frames N] [--profile-parser]\n"
                "  [--benchmark [--scene text|grid] [--warmup N] [--columns N] [--rows N]\n"
                "               [--animate] [--update-every-frame]]\n"
                "Without --benchmark, opens an input-driven Markdown editor (default 14pt).\n"
                "Ctrl+S saves; Ctrl+Z/Ctrl+Shift+Z undo/redo; Ctrl+C/X/V clipboard; mouse selection and scrolling.\n"
                "An unsaved document requires closing twice to discard. --frames limits editor rendering for smoke "
                "checks.\n"
                "--profile-parser adds nested callback timing; coarse phases and arena counters are always reported.\n"
                "Benchmark defaults: 2000 measured +200 warmup frames, 32px height, 1920x1080.\n",
                argv[0]);
            return 0;
        }
        if (!strcmp(arg, "--benchmark")) {
            opt.benchmark = true;
            continue;
        }
        if (!strcmp(arg, "--profile-parser")) {
            opt.profile_parser = true;
            continue;
        }
        if (!strcmp(arg, "--fullscreen")) {
            opt.fullscreen = true;
            continue;
        }
        if (!strcmp(arg, "--animate")) {
            opt.animate = true;
            continue;
        }
        if (!strcmp(arg, "--update-every-frame")) {
            opt.update_every_frame = true;
            continue;
        }
        if (!strcmp(arg, "--")) {
            if (opt.file || i + 2 != argc) {
                fprintf(stderr, "Expected one file after --\n");
                return 2;
            }
            opt.file = argv[++i];
            break;
        }
        if (arg[0] != '-') {
            if (opt.file) {
                fprintf(stderr, "Only one document may be opened\n");
                return 2;
            }
            opt.file = arg;
            continue;
        }
        if (++i == argc) {
            fprintf(stderr, "Missing value for %s\n", arg);
            return 2;
        }
        if (!strcmp(arg, "--file")) {
            if (opt.file) {
                fprintf(stderr, "Only one document may be opened\n");
                return 2;
            }
            opt.file = argv[i];
            continue;
        }
        if (!strcmp(arg, "--scene")) {
            if (!strcmp(argv[i], "grid"))
                opt.grid = true;
            else if (!strcmp(argv[i], "text"))
                opt.grid = false;
            else {
                fprintf(stderr, "Scene must be text or grid\n");
                return 2;
            }
            continue;
        }
        char* end;
        if (!strcmp(arg, "--font-pt")) {
            f32 value = strtof(argv[i], &end);
            if (!*argv[i] || *end || !isfinite(value) || value < 1 || value > 128) {
                fprintf(stderr, "Font points must be a number in 1..128\n");
                return 2;
            }
            opt.font_pt = value;
            size_set    = true;
            continue;
        }
        unsigned long value = strtoul(argv[i], &end, 10);
        if (!*argv[i] || *end || value > 1000000 || (!value && strcmp(arg, "--warmup"))) {
            fprintf(stderr, "Invalid numeric value for %s: %s\n", arg, argv[i]);
            return 2;
        }
        if (!strcmp(arg, "--frames"))
            opt.frames = (u32)value;
        else if (!strcmp(arg, "--warmup"))
            opt.warmup = (u32)value;
        else if (!strcmp(arg, "--width"))
            opt.width = (u32)value;
        else if (!strcmp(arg, "--height"))
            opt.height = (u32)value;
        else if (!strcmp(arg, "--columns"))
            opt.columns = (u32)value;
        else if (!strcmp(arg, "--rows"))
            opt.rows = (u32)value;
        else if (!strcmp(arg, "--font-size")) {
            opt.font_size = (f32)value;
            opt.font_pt   = 0;
            size_set      = true;
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg);
            return 2;
        }
    }
    if ((u64)opt.columns * opt.rows > 65536 || opt.width < 64 || opt.height < 64 || opt.width > 16384 ||
        opt.height > 16384 || (opt.font_pt == 0 && (opt.font_size < 4 || opt.font_size > 512))) {
        fprintf(stderr, "Limits: grid <=65536 cells, extent64..16384, pixel font size4..512\n");
        return 2;
    }
    if (opt.benchmark) {
        if (opt.file) {
            fprintf(stderr, "Use document mode with --frames to measure a file\n");
            return 2;
        }
        if (opt.profile_parser) {
            fprintf(stderr, "--profile-parser requires document mode\n");
            return 2;
        }
        if (!opt.frames)
            opt.frames = 2000;
        return run_benchmark(&opt);
    }
    if (opt.grid || opt.animate || opt.update_every_frame) {
        fprintf(stderr, "Synthetic scene/update options require --benchmark\n");
        return 2;
    }
    if (!size_set)
        opt.font_pt = 14;
    return run_editor(&opt);
}
