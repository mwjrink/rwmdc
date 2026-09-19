#pragma once

#include <lib/grim/gfx/internal_graphics.h>

void empty_garbage(rop(rw RenderState) render_state) {
    rop(ro GraphicsContext) ctx = render_state->r_ctx->ctx;
    vkDeviceWaitIdle(ctx->device);
    SwapchainGarbage garbage;
    while (ring_buff_peek(&render_state->swapchain_garbage, &garbage)) {
        ring_buff_pop(&render_state->swapchain_garbage, &garbage);
        dispose_swapchain_garbage(ctx, &garbage);
    }
}

void process_garbage(rop(rw RenderState) render_state) {
    rop(ro GraphicsContext) ctx  = render_state->r_ctx->ctx;
    SwapchainGarbage garbage     = {0};
    u32              read_result = ring_buff_peek(&render_state->swapchain_garbage, &garbage);
    if (read_result == true) {
        if (check_dispose_ready(ctx, &garbage)) {
            ring_buff_pop(&render_state->swapchain_garbage, &garbage);
            dispose_swapchain_garbage(ctx, &garbage);
        }
    }
}

RenderContext render_context_create(rop(rw Arena) arena,
                                    rop(ro GraphicsContext) ctx,
                                    rop(rw RenderTarget) render_target) {
    u32 frames_in_flight = render_target->swapchain.image_count;

    Pipeline* glyph_pipeline = arena_alloc_align(arena, sizeof(void*), sizeof(Pipeline));
    *glyph_pipeline          = create_glyph_pipeline(arena, ctx, render_target);

    Pipeline* rect_pipeline = arena_alloc_align(arena, sizeof(void*), sizeof(Pipeline));
    *rect_pipeline          = create_rect_pipeline(arena, ctx, render_target);

    RenderContext render_context = {
        .ctx              = ctx,
        .target           = render_target,
        .glyph_pipeline   = glyph_pipeline,
        .rect_pipeline    = rect_pipeline,
        .frames_in_flight = frames_in_flight,
    };

    rop(ro QueueFamilies) families = &ctx->queue_families;
    if (families->families[families->graphics_idx].command_pool.handle == VK_NULL_HANDLE) {

        CommandPool pool              = create_command_pool(ctx, families->graphics_idx);
        rop(rw CommandBuffer) buffers = create_command_buffers(arena, ctx, &pool, frames_in_flight);
        pool.command_buffers          = buffers;

        families->families[families->graphics_idx].command_pool = pool;
    }

    return render_context;
}

RenderState create_render_state(rop(rw Arena) arena, rop(rw RenderContext) r_ctx, rop(ro FontAtlas) font_atlas) {
    rop(ro GraphicsContext) ctx = r_ctx->ctx;

    u64 glyph_bounds_bytes  = (u64)font_atlas->glyph_count * sizeof(GlyphBounds);
    u64 glyph_bands_bytes   = (u64)font_atlas->glyph_count * sizeof(GlyphBands);
    u64 packed_curves_bytes = (u64)font_atlas->curve_count * sizeof(PackedCurve);
    u64 band_words_bytes    = (u64)font_atlas->band_word_count * sizeof(u32);

    u64 glyph_bounds_off  = 0;
    u64 glyph_bands_off   = (glyph_bounds_off + glyph_bounds_bytes + 15) & ~(u64)15;
    u64 packed_curves_off = (glyph_bands_off + glyph_bands_bytes + 15) & ~(u64)15;
    u64 band_words_off    = (packed_curves_off + packed_curves_bytes + 15) & ~(u64)15;

    // One device-local buffer holds the whole atlas; one contiguous upload.
    Buffer slug_buff = buffer_create(ctx,
                                     font_atlas->total_bytes,
                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                         VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    buffer_copy_with_staging(arena, ctx, slug_buff, font_atlas->data, font_atlas->total_bytes);

    PushConstant_TextVert pc_vert = {
        .draw_glyphs        = slug_buff.address,
        .font_glyphs_bounds = slug_buff.address + glyph_bounds_off,
        .pixel_to_ndc       = (Vec2){0},
        .scroll_offset      = (Vec2){0},
        .draw_glyph_count   = 0,
    };
    PushConstant_TextFrag pc_frag = {
        .font_glyph_bands = slug_buff.address + glyph_bands_off,
        .font_curves      = slug_buff.address + packed_curves_off,
        .font_bands       = slug_buff.address + band_words_off,
    };
    PushConstant_Rect pc_rect = {
        .rects         = 0,
        .pixel_to_ndc  = (Vec2){0},
        .scroll_offset = (Vec2){0},
        .rect_count    = 0,
    };

    RenderState render_state = {
        .r_ctx = r_ctx,

        .image_idx   = 0,
        .frame_count = 0,

        .slug_buff = slug_buff,

        .pc_vert = pc_vert,
        .pc_frag = pc_frag,
        .pc_rect = pc_rect,

        .swapchain_garbage = ring_buff_create_with_cap(arena, sizeof(SwapchainGarbage), 5),
    };

    return render_state;
}

// Returns whether or not rendering the frame should continue or restart
u8 start_frame(rop(rw Arena) arena, rop(rw RenderState) render_state) {
    rop(rw RenderContext) r_ctx        = render_state->r_ctx;
    rop(ro GraphicsContext) ctx        = r_ctx->ctx;
    rop(rw RenderTarget) render_target = r_ctx->target;

    u32 frames_in_flight = r_ctx->frames_in_flight;

    u32 frame_idx = render_state->frame_count % frames_in_flight;
    rop(ro CommandBuffer) command_buffers =
        ctx->queue_families.families[ctx->queue_families.graphics_idx].command_pool.command_buffers;

    rop(ro SyncObjects) sync_objects = &(render_target->swapchain.sync_objects[frame_idx]);
    vkWaitForFences(ctx->device, 1, &(sync_objects->open_on_render_finish), VK_TRUE, UINT64_MAX);
    VkResult result = vkAcquireNextImageKHR(ctx->device,
                                            render_target->swapchain.handle,
                                            UINT64_MAX,
                                            sync_objects->signal_on_image_available,
                                            VK_NULL_HANDLE,
                                            &render_state->image_idx);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        DEBUG_LOG(SCOPE_GFX_SWAPCHAIN, "Start Frame resize");
        render_target_update_extent(render_target);

        SwapchainGarbage new_garbage  = recreate_swapchain(arena, ctx, render_target);
        u32              garbage_full = ring_buff_push(&render_state->swapchain_garbage, &new_garbage);
        if (unlikely(garbage_full == true)) {
            empty_garbage(render_state);
        }
        r_ctx->render_target_resized = false;

        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        CRITICAL_LOG(SCOPE_GFX_SWAPCHAIN, "Failed to Acquire Next Image.");
        exit(1);
    }

    vkResetFences(ctx->device, 1, &(sync_objects->open_on_render_finish));
    vkResetCommandBuffer(command_buffers[frame_idx].handle, 0);

    return true;
}

void draw_frame(rop(rw RenderState) render_state) {
    u32 frames_in_flight = render_state->r_ctx->frames_in_flight;

    rop(rw RenderContext) r_ctx        = render_state->r_ctx;
    rop(ro GraphicsContext) ctx        = r_ctx->ctx;
    rop(rw RenderTarget) render_target = r_ctx->target;

    u32 frame_idx = render_state->frame_count % frames_in_flight;

    rop(ro CommandBuffer) command_buffers =
        ctx->queue_families.families[ctx->queue_families.graphics_idx].command_pool.command_buffers;

    RecordingState recording_state =
        begin_recording(ctx, &command_buffers[frame_idx], r_ctx->glyph_pipeline, render_target, frame_idx);

    VkViewport viewport = {0};
    viewport.x          = 0.0f;
    viewport.y          = 0.0f;
    viewport.width      = (f32)render_target->extent.width;
    viewport.height     = (f32)render_target->extent.height;
    viewport.minDepth   = 0.0f;
    viewport.maxDepth   = 1.0f;
    vkCmdSetViewport(recording_state.cmd_buffer->handle, 0, 1, &viewport);

    VkRect2D scissor = {0};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent   = render_target->extent;
    vkCmdSetScissor(recording_state.cmd_buffer->handle, 0, 1, &scissor);

    VkImageMemoryBarrier2 barrier = {0};
    barrier.sType                 = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask          = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.srcAccessMask         = 0;
    barrier.dstStageMask          = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.dstAccessMask         = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout             = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout             = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    barrier.image                 = render_target->swapchain.images[render_state->image_idx].handle;
    barrier.subresourceRange      = (VkImageSubresourceRange){
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1,
        .layerCount = 1,
    };

    VkDependencyInfo barrier_dependency_info        = {0};
    barrier_dependency_info.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    barrier_dependency_info.imageMemoryBarrierCount = 1;
    barrier_dependency_info.pImageMemoryBarriers    = &barrier;

    vkCmdPipelineBarrier2(recording_state.cmd_buffer->handle, &barrier_dependency_info);

    VkRenderingAttachmentInfo color_attachment_info = {0};
    color_attachment_info.sType                     = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment_info.imageView                 = render_target->swapchain.images[render_state->image_idx].view;
    color_attachment_info.imageLayout               = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    color_attachment_info.loadOp                    = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment_info.storeOp                   = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment_info.clearValue                = (VkClearValue){.color = {{0.0f, 0.0f, 0.0f, 1.0f}}};

    VkRenderingInfo rendering_info      = {0};
    rendering_info.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea           = (VkRect2D){.offset = {0, 0}, .extent = render_target->extent};
    rendering_info.layerCount           = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments    = &color_attachment_info;
    vkCmdBeginRendering(recording_state.cmd_buffer->handle, &rendering_info);

    // Pass 1: highlight rects (depth 0, behind glyphs).
    if (render_state->pc_rect.rect_count) {
        cmd_bind_pipeline(ctx, &recording_state, r_ctx->rect_pipeline);
        vkCmdPushConstants(recording_state.cmd_buffer->handle,
                           r_ctx->rect_pipeline->layout,
                           VK_SHADER_STAGE_VERTEX_BIT,
                           0,
                           sizeof(PushConstant_Rect),
                           &render_state->pc_rect);
        cmd_draw(&recording_state, render_state->pc_rect.rect_count);
    }

    // Pass 2: glyphs (depth 1).
    if (render_state->pc_vert.draw_glyph_count) {
        cmd_bind_pipeline(ctx, &recording_state, r_ctx->glyph_pipeline);
        vkCmdPushConstants(recording_state.cmd_buffer->handle,
                           r_ctx->glyph_pipeline->layout,
                           VK_SHADER_STAGE_VERTEX_BIT,
                           0,
                           sizeof(PushConstant_TextVert),
                           &render_state->pc_vert);
        vkCmdPushConstants(recording_state.cmd_buffer->handle,
                           r_ctx->glyph_pipeline->layout,
                           VK_SHADER_STAGE_FRAGMENT_BIT,
                           sizeof(PushConstant_TextVert),
                           sizeof(PushConstant_TextFrag),
                           &render_state->pc_frag);
        cmd_draw(&recording_state, render_state->pc_vert.draw_glyph_count);
    }

    // Pass 3: UI rects (depth 2). Reuses the rect pipeline/pattern.
    // TODO bind a distinct rect range for UI once the UI layer exists.

    vkCmdEndRendering(recording_state.cmd_buffer->handle);

    VkImageMemoryBarrier2 barrier_present = {0};
    barrier_present.sType                 = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier_present.srcStageMask          = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier_present.srcAccessMask         = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier_present.dstStageMask          = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier_present.dstAccessMask         = 0;
    barrier_present.oldLayout             = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    barrier_present.newLayout             = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier_present.image                 = render_target->swapchain.images[render_state->image_idx].handle;
    barrier_present.subresourceRange      = (VkImageSubresourceRange){
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1,
        .layerCount = 1,
    };

    VkDependencyInfo present_dependency_info        = {0};
    present_dependency_info.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    present_dependency_info.imageMemoryBarrierCount = 1;
    present_dependency_info.pImageMemoryBarriers    = &barrier_present;

    vkCmdPipelineBarrier2(recording_state.cmd_buffer->handle, &present_dependency_info);

    end_recording(ctx, &recording_state);

    enqueue_submit_graphics(ctx, render_target, command_buffers, frame_idx, render_state->image_idx);
}

void end_frame(rop(rw Arena) arena, rop(rw RenderState) render_state) {
    rop(rw RenderContext) r_ctx        = render_state->r_ctx;
    rop(ro GraphicsContext) ctx        = r_ctx->ctx;
    rop(rw RenderTarget) render_target = r_ctx->target;

    VkResult present_result = enqueue_present(ctx, render_target, render_state->image_idx);

    if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR ||
        r_ctx->render_target_resized) {
        DEBUG_LOG(SCOPE_GFX_PRESENT, "End Frame resize");
        render_target_update_extent(render_target);

        SwapchainGarbage garbage      = recreate_swapchain(arena, ctx, render_target);
        u32              garbage_full = ring_buff_push(&render_state->swapchain_garbage, &garbage);
        if (unlikely(garbage_full == true)) {
            empty_garbage(render_state);
        }

        r_ctx->render_target_resized = false;
    } else if (present_result != VK_SUCCESS) {
        CRITICAL_LOG(SCOPE_GFX_PRESENT, "Failed to Queue Present.");
        exit(1);
    } else {
        process_garbage(render_state);
    }

    render_state->frame_count += 1;
}

void cleanup_render_state(rop(rw RenderState) render_state) {
    rop(ro GraphicsContext) ctx = render_state->r_ctx->ctx;

    empty_garbage(render_state);

    cleanup_buffer(ctx, &render_state->slug_buff);
}

void cleanup_render_context(rop(rw RenderContext) r_ctx) {
    rop(ro GraphicsContext) ctx = r_ctx->ctx;

    rop(ro QueueFamilies) families = &ctx->queue_families;
    cleanup_command_pool(ctx, &families->families[families->graphics_idx].command_pool);
    cleanup_pipeline(ctx, r_ctx->glyph_pipeline);
    cleanup_pipeline(ctx, r_ctx->rect_pipeline);
}
