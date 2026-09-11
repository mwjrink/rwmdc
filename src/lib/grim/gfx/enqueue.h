#pragma once

void enqueue_submit_graphics(RenderState *rs) {
    RenderContext *rc = rs->r_ctx;
    const GraphicsContext *ctx = rc->ctx;
    FrameSlot *slot = &rc->slots[rs->frame_count % rc->frames_in_flight];
    VkSemaphoreSubmitInfo wait = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = slot->image_available, .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphoreSubmitInfo signal = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = rc->target->swapchain.render_finished[rs->image_idx], .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
    VkCommandBufferSubmitInfo command = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = slot->command_buffer};
    VkSubmitInfo2 submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1, .pWaitSemaphoreInfos = &wait,
        .commandBufferInfoCount = 1, .pCommandBufferInfos = &command,
        .signalSemaphoreInfoCount = 1, .pSignalSemaphoreInfos = &signal};
    // Reset only when a successful acquire will actually be submitted.
    check_vkresult(vkResetFences(ctx->device, 1, &slot->fence), SCOPE_GFX_COMMAND_QUEUE, "Reset submission fence");
    check_vkresult(vkQueueSubmit2(ctx->graphics_queue, 1, &submit, slot->fence), SCOPE_GFX_COMMAND_QUEUE, "Submit text frame");
}

void end_frame(Arena *arena, RenderState *rs) {
    (void)arena;
    RenderContext *rc = rs->r_ctx;
    Swapchain *swapchain = &rc->target->swapchain;
    VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &swapchain->render_finished[rs->image_idx],
        .swapchainCount = 1, .pSwapchains = &swapchain->handle, .pImageIndices = &rs->image_idx};
    VkResult result = vkQueuePresentKHR(rc->ctx->present_queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) rc->render_target_resized = true;
    else check_vkresult(result, SCOPE_GFX_PRESENT, "Present text frame");
    rs->frame_count++;
}
