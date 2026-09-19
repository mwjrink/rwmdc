#include <lib/grim/assert.h>
#include <lib/grim/bp.h>
#include <lib/grim/gfx/graphics.h>
#include <lib/grim/logger.h>
#include <lib/grim/mem/arena.h>
#include <lib/grim/os/input_linux.h>

// FIXME TEMP
#include <lib/grim/gfx/internal_graphics.h>

#define RWMD_MARKDOWN_IMPLEMENTATION
#include <lib/grim/markdown/layout.h>
#undef RWMD_MARKDOWN_IMPLEMENTATION

int main(int argc, char** argv) {
    Arena        general_arena = arena_create();
    ScratchArena scratch_arena = scratch_create();

    InputContext input_ctx = input_ctx_create(&general_arena);

    RenderTarget render_target = window_create(&general_arena, 1920, 1080);

    GraphicsContext gfx_ctx = graphics_context_create(&general_arena, &render_target);

    RenderContext render_ctx = render_context_create(&general_arena, &gfx_ctx, &render_target);

    InputState  input_state  = input_create_state(&input_ctx);
    RenderState render_state = create_render_state(&general_arena, &render_ctx);

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (strcmp(arg, "--help") == 0) {
            printf("Usage: %s [file.md]", argv[0]);
            return 0;
        }
    }
    // TODO these should return a checkpoint I can pass back in at rollback/pop stage
    arena_ckpt(&general_arena);

    // TODO check fps before descriptor set commit & after, feels like MASSIVE regression

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
            while (render_target.window->width == 0 || render_target.window->height == 0) {
                window_poll_events(render_target.window);

                nanosleep(&ts, &ts);

                // createSwapChain();
                // createImageViews();
                // createFramebuffers();
            }

            // TODO only update this after the swapchain was updated?
            camera_update_lens(&camera, render_target.window->width, render_target.window->height);
        }

        time_checkpoint(SCOPE_DEBUG, "window events", app_profiling_time);

        input_update(&input_state);

        time_checkpoint(SCOPE_DEBUG, "input events", app_profiling_time);

        if (input_state.lock_mouse) {         // && render_target.window->locked_pointer == NULL) {
            window_lock_pointer(render_target.window);
        } else if (!input_state.lock_mouse) { // && render_target.window->locked_pointer != NULL) {
            window_unlock_pointer(render_target.window);
        }

        time_checkpoint(SCOPE_DEBUG, "pointer lock", app_profiling_time);

        // anim_walk_forwards(&scratch_arena, &anim_ctx, &anim_state);
        // anim_update(&scratch_arena, &anim_state, dt);
        // scratch_reset(&scratch_arena);

        // FIX TEMP
        // Rotor3 stripped   = camera.current.direction;
        // stripped.bivec.yz = 0.0f;
        // stripped.bivec.xy = 0.0f;
        // rotor3_normalize_ip(&stripped);
        // player.physics_entity->rotation = stripped;

        // player_move(&player, &input_state);
        // player_update(&player, dt);
        //
        // // DEBUG_LOG("<FRAME");
        // // // dump_vec3(player.physics_entity->position);
        // // dump_rotor3(player.physics_entity->rotation);
        // // DEBUG_LOG("FRAME>");
        //
        // Transform player_transform = player.physics_entity->transform;
        // // player_transform.rotation  = rotor3_from_vec3_to_vec3(
        // //     vec3_unit_z(), vec3_normalize(vec3_sub(player.physics_entity->position, camera.target.position)));
        // // camera_set_target(&camera, player_transform);
        // camera_set_target_pos(&camera, player_transform.pos);
        //
        // if (input_state.lock_cam) {
        //     // ideally slow the glerping here
        //     // also maybe we need something other than a glerp?
        //     // the expensive slerp?
        //     camera_set_target_dir(&camera, player_transform.rotation);
        //     // dump_rotor3(camera.current.direction);
        //
        //     // if (input_ctx.key_delete) {
        //     //     DEBUG_LOG("Current: ");
        //     //     dump_rotor3(camera.current.direction);
        //     //     DEBUG_LOG("Target: ");
        //     //     dump_rotor3(camera.target.direction);
        //     //     DEBUG_LOG(" - ");
        //     // }
        // } else {
        //     camera_rotate_yaw_pitch(
        //         &camera, (f32)input_state.cam_right * 1.0f * dt, (f32)input_state.cam_forward * 1.0f * dt);
        // }

        if (input_state.lock_mouse) {
            camera_rotate_yaw_pitch(
                &camera, (f32)input_state.cam_right * 1.0f * dt, (f32)input_state.cam_forward * 1.0f * dt);
            camera_zoom(&camera, input_state.up * dt);
            camera_rotate_yaw_pitch(&camera, (f32)input_state.right * 1.0f * dt, (f32)input_state.forward * 1.0f * dt);
        } else {
            camera_rotate_yaw_pitch(
                &camera, (f32)input_state.cam_right * 1.0f * dt, (f32)input_state.cam_forward * 1.0f * dt);
            // BUG this is moving the camera, NOT the target?
            camera_translate_target(&camera,
                                    (Vec3){
                                        .x = 100.0f * input_state.right * dt,
                                        .y = 10.0f * input_state.up * dt,
                                        .z = 100.0f * input_state.forward * dt,
                                    });
        }

        // TODO this seems to have about 1200ns of latency. From printing + gettime??
        time_checkpoint(SCOPE_DEBUG, "camera input", app_profiling_time);

        camera_update(&camera, dt);
        time_checkpoint(SCOPE_DEBUG, "camera update", app_profiling_time);
        camera_matrices = camera_get_matrices(&camera);
        time_checkpoint(SCOPE_DEBUG, "camera get mats", app_profiling_time);

        camera_ubo.world_to_clip = mat4_mul(camera_matrices.view_to_clip, camera_matrices.world_to_view);
        // TODO this should come from camera_matrices, rename to cam_state
        camera_ubo.cam_offset    = camera_matrices.cam_location;

        scene_data_update(&render_state, camera_ubo);
        // dump_mat4(camera_ubo.world_to_clip);

        time_checkpoint(SCOPE_DEBUG, "scene_data_update", app_profiling_time);

        // physics_update(&scratch_arena, &physics_state, dt);
        //
        // time_checkpoint(SCOPE_DEBUG, "physics_update", app_profiling_time);

        // TODO check if render_target is 0 size and just loop on
        // poll input/events, this applies to minimized window
        u32 start_frame_result = start_frame(&general_arena, &render_state);
        if (!start_frame_result) {
            continue;
        }

        time_checkpoint(SCOPE_DEBUG, "start_frame", app_profiling_time);

        // TODO use this!
        // instance_update(dragon_inst, model_ubo);

        draw_frame(&render_state);

        time_checkpoint(SCOPE_DEBUG, "draw_frame", app_profiling_time);

        end_frame(&general_arena, &render_state);

        time_checkpoint(SCOPE_DEBUG, "end_frame", app_profiling_time);

        if (render_target.window->request_close == true) {
            break;
        }

        arena_rollback(&general_arena);
        assert(SCOPE_MEM_ARENA, frame_start_arena_len == general_arena.len);

        time_checkpoint(SCOPE_DEBUG, "arena_rollback", app_profiling_time);

        scratch_reset(&scratch_arena);

        time_checkpoint(SCOPE_DEBUG, "scratch_reset", app_profiling_time);

        // if (render_state.frame_count > 5) {
        // break;
        // }
    }

    f32 end       = (f32)clock() / (f32)CLOCKS_PER_SEC;
    f32 full_time = end - start;
    DEBUG_LOG(SCOPE_DEBUG,
              "%u frames in %fs for %f fps",
              render_state.frame_count,
              full_time,
              (f32)render_state.frame_count / full_time);

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
