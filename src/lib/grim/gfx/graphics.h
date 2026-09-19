#pragma once

#include <lib/grim/assert.h>
#include <lib/grim/bp.h>
#include <lib/grim/math.h>
#include <lib/grim/mem/arena.h>
#include <lib/grim/mem/buff.h>

// TODO find a way to do this & share the vertex type
// Maybe a separate gfx_types.h file that does not include
// this then we include that and rune types here?
#include <lib/grim/gfx/types.h>
#include <lib/grim/rune/types.h>

// ====================================================================================================================
// =                                                    Types                                                         =
// ====================================================================================================================

// TODO I need a static assert that this is byte identical to Cluster becuase right now I treat them as such
// BETTER: Get rid of this or the other one. No need to have 2 copies
typedef struct GpuCluster {
    alignas(8) u32 idx_offset;
    u32 vtx_offset;

    struct {
        u8 vtx_count;
        u8 tri_count;

        u8 _padding[2];
    };

    ClusterBounds bounds;
} GpuCluster;
STATIC_ASSERT(sizeof(GpuCluster) == 32);

#include <vulkan/vulkan.h>

// ====================================================================================================================
// =                                                  Functions                                                       =
// ====================================================================================================================

// ================  Render Target  ================

RenderTarget window_create(rop(rw Arena) arena, ro u32 width, ro u32 height);

// ================  Context  ================

GraphicsContext graphics_context_create(rop(rw Arena) arena, rop(rw RenderTarget) render_target);

void cleanup_graphics_ctx(rop(rw GraphicsContext) ctx);
void cleanup_render_target(rop(ro GraphicsContext) ctx, rop(rw RenderTarget) render_target);
void render_target_update_extent(rop(rw RenderTarget) render_target);

// ================  Command Pool & Buffer ================

CommandPool    create_command_pool(rop(ro GraphicsContext) ctx, ro u32 queue_idx);
CommandPool    create_command_pool(rop(ro GraphicsContext) ctx, ro u32 queue_idx);
CommandBuffer* create_command_buffers(rop(rw Arena) arena,
                                      rop(ro GraphicsContext) ctx,
                                      rop(rw CommandPool) pool,
                                      ro u32 buffer_count);

void cleanup_command_pool(rop(ro GraphicsContext) ctx, rop(rw CommandPool) command_pool);
// void cleanup_command_buffer(rop(ro GraphicsContext) ctx, CommandPool* command_pool);

// ================  Commands  ================

RecordingState begin_recording(rop(ro GraphicsContext) ctx,
                               rop(ro CommandBuffer) command_buffer,
                               rop(ro Pipeline) pipeline,
                               rop(ro RenderTarget) render_target,
                               ro u32 frame_idx);
void           end_recording(rop(ro GraphicsContext) ctx, rop(rw RecordingState) state);

// ================  Buffer  ================

// TODO not sure if this should be internal
Buffer buffer_create(rop(ro GraphicsContext) ctx, u64 size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
void   cleanup_buffer(rop(ro GraphicsContext) ctx, rop(rw Buffer) buffer);

// ================  Image  ================

Image create_image(rop(ro GraphicsContext) ctx,
                   u32                      width,
                   u32                      height,
                   VkFormat                 format,
                   VkImageTiling            tiling,
                   VkImageUsageFlagBits     usage,
                   VkMemoryPropertyFlagBits mem_properties);

// ================  Rendering  ================

RenderContext render_context_create(rop(rw Arena) arena,
                                    rop(ro GraphicsContext) ctx,
                                    rop(rw RenderTarget) render_target);
RenderState   create_render_state(rop(rw Arena) arena, rop(rw RenderContext) r_ctx);
AssetHandle   load_model(rop(rw RenderState) render_state, rop(ro Model) model);

u8   start_frame(rop(rw Arena) arena, rop(rw RenderState) render_state);
u8   update_cam_ubo_frame(rop(rw Arena) arena, rop(rw RenderState) render_state, rop(ro CameraUbo) ubo);
u8   update_model_ubos_frame(rop(rw Arena) arena, rop(rw RenderState) render_state, AllocDynList ubo);
void draw_frame(rop(rw RenderState) render_state);
void end_frame(rop(rw Arena) arena, rop(rw RenderState) render_state);

void cleanup_render_state(rop(rw RenderState) render_state);
void cleanup_render_context(rop(rw RenderContext) r_ctx);

// ====================================================================================================================
// =                                                  Imports =
// ====================================================================================================================
