#pragma once

#include <lib/grim/gfx/internal_graphics.h>

VkResult enqueue_present(rop(ro GraphicsContext) ctx, rop(ro RenderTarget) render_target, ro u32 image_idx) {
    VkPresentInfoKHR present_info   = {0};
    present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores    = &(render_target->swapchain.sync_objects[image_idx].signal_on_render_finish);
    present_info.swapchainCount     = 1;
    present_info.pSwapchains        = &(render_target->swapchain.handle);
    present_info.pImageIndices      = &image_idx;
    present_info.pResults           = NULL;

    VkFence present_complete_fence = render_target->swapchain.sync_objects[image_idx].open_on_present_complete;
    // TODO never wait
    vkWaitForFences(ctx->device, 1, &present_complete_fence, VK_TRUE, u64_MAX);
    vkResetFences(ctx->device, 1, &present_complete_fence);

    VkSwapchainPresentFenceInfoKHR present_fence = {0};
    present_fence.sType                          = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR;
    present_fence.swapchainCount                 = 1;
    present_fence.pFences                        = &present_complete_fence;

    present_info.pNext = &present_fence;

    VkResult result = vkQueuePresentKHR(ctx->queue_families.queues[ctx->queue_families.present_idx], &present_info);
    // check_vkresult(result, "Failed to Present.");
    // TODO this is a special case because this can return out_of_date framebuffer etc
    return result;
}

void enqueue_submit_graphics(rop(ro GraphicsContext) ctx,
                             rop(ro RenderTarget) render_target,
                             rop(ro CommandBuffer) buffers,
                             ro u32 frame_idx,
                             ro u32 image_idx) {
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    VkSubmitInfo submit_info         = {0};
    submit_info.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount   = 1;
    submit_info.pWaitSemaphores      = &(render_target->swapchain.sync_objects[frame_idx].signal_on_image_available);
    // this tells the GPU WHEN to wait on the semaphore which we want at present
    submit_info.pWaitDstStageMask    = wait_stages;
    submit_info.commandBufferCount   = 1;
    submit_info.pCommandBuffers      = &(buffers[frame_idx].handle);
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores    = &(render_target->swapchain.sync_objects[image_idx].signal_on_render_finish);

    VkResult result = vkQueueSubmit(ctx->queue_families.queues[ctx->queue_families.graphics_idx],
                                    1,
                                    &submit_info,
                                    render_target->swapchain.sync_objects[frame_idx].open_on_render_finish);
    check_vkresult(result, SCOPE_GFX_COMMAND_QUEUE, "Failed to submit queue for execution.");
}
