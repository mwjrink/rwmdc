#pragma once

#include <lib/grim/gfx/internal_graphics.h>

RecordingState begin_recording(rop(ro GraphicsContext) ctx,
                               rop(ro CommandBuffer) command_buffer,
                               rop(ro Pipeline) pipeline,
                               rop(ro RenderTarget) render_target,
                               ro u32 frame_idx) {
    RecordingState state = {0};
    state.cmd_buffer     = command_buffer;
    state.pipeline       = pipeline;
    state.render_target  = render_target;
    state.frame_idx      = frame_idx;

    command_buffer_begin_record(ctx, command_buffer, false);

    state.currently_recording = true;

    return state;
}

void cmd_bind_pipeline(rop(ro GraphicsContext) ctx, rop(ro RecordingState) state, rop(ro Pipeline) pipeline) {
    vkCmdBindPipeline(state->cmd_buffer->handle, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle);
}

void cmd_draw(rop(ro RecordingState) state, u32 instance_count) {
    vkCmdDraw(state->cmd_buffer->handle, 4, instance_count, 0, 0);
}

void cmd_image_transition(rop(ro GraphicsContext) ctx,
                          rop(ro CommandBuffer) cmd_buffer,
                          VkImage       image,
                          VkImageLayout src_layout,
                          VkImageLayout dst_layout) {
    VkImageMemoryBarrier barrier = {0};
    barrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout            = src_layout;
    barrier.newLayout            = dst_layout;

    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.image                           = image;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;

    VkPipelineStageFlags src_stage;
    VkPipelineStageFlags dst_stage;

    if (src_layout == VK_IMAGE_LAYOUT_UNDEFINED && dst_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (src_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               dst_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;

        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (dst_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
        barrier.dstAccessMask = 0;

        src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        CRITICAL_LOG(SCOPE_GFX_COMMANDS, "Unsupported layout transition.");
        exit(1);
    }

    vkCmdPipelineBarrier(cmd_buffer->handle, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

void end_recording(rop(ro GraphicsContext) ctx, rop(rw RecordingState) state) {
    command_buffer_end_record(ctx, state->cmd_buffer);
    state->currently_recording = false;

    state->cmd_buffer    = NULL;
    state->pipeline      = NULL;
    state->render_target = NULL;
    state->frame_idx     = -1;
}
