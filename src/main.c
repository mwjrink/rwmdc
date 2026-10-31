#include <lib/grim/assert.h>
#include <lib/grim/bp.h>
#include <lib/grim/gfx/graphics.h>
#include <lib/grim/logger.h>
#include <lib/grim/mem/arena.h>
#include <lib/grim/os/input_linux.h>
#include <lib/grim/text/font.h>
#include <lib/grim/text/slug.h>

#include <config.h>
#include <document.h>
#include <editor.h>
// #include <lib/md/md_parser_scratch.h>

// FIXME TEMP
#include <lib/grim/gfx/internal_graphics.h>

// Fontshaper: compile the native shaping font once at startup. One TU owns
// the implementation; the compiled blob lives as long as the editor.
#define FONTSHAPER_STATIC
#define FONTSHAPER_IMPLEMENTATION
#define FONTSHAPER_ASSERT(condition) assert(SCOPE_STARTUP, (condition))
#pragma push_macro("Unsorted")
#undef Unsorted
#include <lib/fontshaper/fontshaper.h>
#pragma pop_macro("Unsorted")
#undef FONTSHAPER_IMPLEMENTATION

int main(int argc, char** argv) {
    u64 app_profiling_time = time_start();

    Str document_path = {0};
    for (i32 i = 1; i < argc; i++) {
        Str arg = str_from_cstring(argv[i]);
        if (str_equals(arg, str_create("--help")) || str_equals(arg, str_create("-h"))) {
            printf("Usage: %s [file.md]", argv[0]);
            return 0;
        } else if (str_equals(arg, str_create("--config")) || str_equals(arg, str_create("-c"))) {
            // TODO config file loc
        } else {
            document_path = arg;
        }
    }

    assert(SCOPE_LOAD, document_path.len != 0);

    Arena        general_arena = arena_create();
    ScratchArena scratch_arena = scratch_create();

    InputContext input_ctx = input_ctx_create(&general_arena);

    RenderTarget render_target = window_create(&general_arena, 1920, 1080);

    GraphicsContext gfx_ctx = graphics_context_create(&general_arena, &render_target);

    RenderContext render_ctx = render_context_create(&general_arena, &gfx_ctx, &render_target);

    Theme       theme;
    AppSettings settings;
    Str         font_path;
    {
        ConfigParseResult parsed_config = config_load(&scratch_arena);

        theme     = parsed_config.theme;
        settings  = parsed_config.settings;
        font_path = parsed_config.font_path;
    }

    // TODO list:
    // - load/parse the config
    //   - use config to load font
    // - create editor context/state
    // - load file
    // - create document
    // - set up saving thread

    // TODO use a loading arena here which is separate. ENSURE scratch is NOT reset internally
    TTFont    font  = font_load(&general_arena, &scratch_arena, font_path.data);
    // TODO get rid of atlas from memory once it's uploaded
    FontAtlas atlas = slug_build_atlas(&general_arena, &scratch_arena, &font);

    // this invalidates font_path
    // scratch_reset(&scratch_arena);
    font_path = (Str){0}; // just to be clear.

    InputState  input_state = input_create_state(&input_ctx);
    // TODO should this be scratch for buffer copy, general for garbage ring buffer
    RenderState render_state =
        create_render_state(&general_arena, &render_ctx, &atlas, (Vec4*)&theme, sizeof(theme) / sizeof(Vec4));

    Editor editor = editor_create(&general_arena, &scratch_arena, &font, settings, document_path);

    // TODO these should return a checkpoint I can pass back in at rollback/pop stage
    arena_ckpt(&general_arena);

    // TODO NEVER call scratch_reset if you don't own scratch arena
    // It's the owner's job to reset it at logical points

    u64 frame_start_arena_len = general_arena.len;
    f32 dt;
    f32 start = (f32)clock() / (f32)CLOCKS_PER_SEC - 0.01f; // give us a bit of dt right at the start

    time_checkpoint(SCOPE_DEBUG, "render loop start", app_profiling_time);

    f32 last_frame_start = start;
    // window_set_fullscreen(render_target.window);
    while (true) {
        f32 now          = (f32)clock() / (f32)CLOCKS_PER_SEC;
        dt               = now - last_frame_start;
        last_frame_start = now;

        // TODO this should be in render_target? not calling window stuff directly
        window_poll_events(render_target.window);

        // TODO this is temp, find a better way. A passthrough bitflag struct to the window struct?
        if (unlikely(render_target.window->width != render_target.extent.width ||
                     render_target.window->height != render_target.extent.height)) {
            render_ctx.render_target_resized = true;

            struct timespec ts;
            ts.tv_sec  = 10 / 1000;
            ts.tv_nsec = (10 % 1000) * 1000000;
            if (render_target.window->width == 0 || render_target.window->height == 0) {
                nanosleep(&ts, &ts);
                continue;
            }
        }

        time_checkpoint(SCOPE_DEBUG, "window events", app_profiling_time);

        input_update(&input_state); // should do a sleep for ~500us or intrinsic to wait on input interrupt. we only
                                    // need to render on input.

        time_checkpoint(SCOPE_DEBUG, "input events", app_profiling_time);

        if (input_state.lock_mouse) { // && render_target.window->locked_pointer == NULL) {
            window_lock_pointer(render_target.window);
        } else if (!input_state.lock_mouse) { // && render_target.window->locked_pointer != NULL) {
            window_unlock_pointer(render_target.window);
        }

        Vec2 pixel_to_ndc = (Vec2){
            .x = 2.0f / (f32)render_target.extent.width,
            .y = 2.0f / (f32)render_target.extent.height,
        };
        // TODO why is scroll passed here? doesn't editor layer handle this?
        update_state(&render_state, editor.scroll, pixel_to_ndc);

        DrawLayer text_draw_layer = editor_get_text_layer(&editor,
                                                          &general_arena,
                                                          &scratch_arena,
                                                          &settings,
                                                          &theme,
                                                          &font,
                                                          render_target.extent.width,
                                                          render_target.extent.height);
        time_checkpoint(SCOPE_DEBUG, "editor_get_text_layer", app_profiling_time);

        RenderLayer text_render_layer = upload_layer(&render_state, &text_draw_layer);
        // DrawLayer ui_layer = editor_get_ui_layer(&editor);

        time_checkpoint(SCOPE_DEBUG, "upload_text_layer", app_profiling_time);

        u32 start_frame_result = start_frame(&general_arena, &render_state);
        if (!start_frame_result) {
            goto render_loop_cleanup;
        }

        time_checkpoint(SCOPE_DEBUG, "start_frame", app_profiling_time);

        RecordingState recording_state = render_init(&render_state, theme.window_bg);

        draw_layer(&render_state, recording_state, &text_render_layer);
        // draw_layer(&render_state, recording_state, &ui_render_layer);

        render_finish(&render_state, recording_state);

        time_checkpoint(SCOPE_DEBUG, "draw_frame", app_profiling_time);

        end_frame(&general_arena, &render_state);

    render_loop_cleanup:
        time_checkpoint(SCOPE_DEBUG, "end_frame", app_profiling_time);

        if (render_target.window->request_close == true) {
            break;
        }

        scratch_reset(&scratch_arena);
        arena_rollback(&general_arena);
        assert(SCOPE_MEM_ARENA, frame_start_arena_len == general_arena.len);

        time_checkpoint(SCOPE_DEBUG, "arena_rollback", app_profiling_time);
    }

    f32 end       = (f32)clock() / (f32)CLOCKS_PER_SEC;
    f32 full_time = end - start;
    WARNING_LOG(SCOPE_DEBUG,
                "%u frames in %fs for %f fps",
                render_state.frame_count,
                (f64)full_time,
                (f64)render_state.frame_count / (f64)full_time);

    // /sys/devices/system/cpu/cpu31/cache/index3/size
    // P & E cores
    // /sys/devices/cpu_core/cpus: Lists all P-cores.
    // /sys/devices/cpu_atom/cpus: Lists all E-cores.

    INFO_LOG(SCOPE_SHUTDOWN, "CLOSING Application!");

    cleanup_render_state(&render_state);

    cleanup_render_context(&render_ctx);
    cleanup_render_target(&gfx_ctx, &render_target);
    cleanup_graphics_ctx(&gfx_ctx);
    close_window(render_target.window);

    input_cleanup(&input_ctx);

    // TODO probably don't need to do this manually.
    // Maybe error check that arena.len = 0 so we can reason about allocs?
    arena_destroy(&general_arena);
    scratch_destroy(&scratch_arena);

    print_log_summary();

    INFO_LOG(SCOPE_SHUTDOWN, "Application CLOSED!");

    return 0;
}
