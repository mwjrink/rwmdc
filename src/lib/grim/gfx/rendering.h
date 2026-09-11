#pragma once

RenderContext render_context_create(Arena *arena, const GraphicsContext *ctx, RenderTarget *target) {
    (void)arena;
    RenderContext rc = {.ctx = ctx, .target = target, .frames_in_flight = 2};
    create_swapchain(ctx, target);
    VkSemaphoreCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    VkCommandPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = ctx->graphics_family};
    for (u32 i = 0; i < rc.frames_in_flight; i++) {
        FrameSlot *slot = &rc.slots[i];
        check_vkresult(vkCreateCommandPool(ctx->device, &pi, NULL, &slot->pool), SCOPE_GFX_COMMAND_BUFFER, "Create frame command pool");
        VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = slot->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
        check_vkresult(vkAllocateCommandBuffers(ctx->device, &ai, &slot->command_buffer), SCOPE_GFX_COMMAND_BUFFER, "Allocate frame command buffer");
        check_vkresult(vkCreateFence(ctx->device, &fi, NULL, &slot->fence), SCOPE_GFX_COMMAND_BUFFER, "Create frame fence");
        check_vkresult(vkCreateSemaphore(ctx->device, &si, NULL, &slot->image_available), SCOPE_GFX_COMMAND_BUFFER, "Create acquisition semaphore");
    }
    return rc;
}

RenderState create_render_state(Arena *arena, RenderContext *rc) {
    (void)arena;
    return (RenderState){.r_ctx = rc};
}

VkCommandBuffer render_command_buffer(const RenderState *rs) {
    return rs->r_ctx->slots[rs->frame_count % rs->r_ctx->frames_in_flight].command_buffer;
}

bool start_frame(Arena *arena, RenderState *rs) {
    (void)arena;
    RenderContext *rc = rs->r_ctx;
    const GraphicsContext *ctx = rc->ctx;
    if (rc->target->window->request_close || !rc->target->window->width || !rc->target->window->height) return false;
    if (rc->render_target_resized || !rc->target->swapchain.handle) {
        // Only resize and shutdown drain the device; ordinary frames wait on their own slot.
        check_vkresult(vkDeviceWaitIdle(ctx->device), SCOPE_GFX_SWAPCHAIN, "Wait for swapchain recreation");
        if (create_swapchain(ctx, rc->target)) rc->render_target_resized = false;
        // Keep frame_count and its query/upload slot association unchanged across resizes.
        return false;
    }
    FrameSlot *slot = &rc->slots[rs->frame_count % rc->frames_in_flight];
    check_vkresult(vkWaitForFences(ctx->device, 1, &slot->fence, VK_TRUE, UINT64_MAX), SCOPE_GFX_COMMAND_BUFFER, "Wait for frame slot");
    VkResult acquired = vkAcquireNextImageKHR(ctx->device, rc->target->swapchain.handle,
        UINT64_MAX, slot->image_available, VK_NULL_HANDLE, &rs->image_idx);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        rc->render_target_resized = true;
        return false;
    }
    if (acquired == VK_SUBOPTIMAL_KHR) rc->render_target_resized = true;
    else check_vkresult(acquired, SCOPE_GFX_SWAPCHAIN, "Acquire swapchain image");
    check_vkresult(vkResetCommandPool(ctx->device, slot->pool, 0), SCOPE_GFX_COMMAND_BUFFER, "Reset completed frame commands");
    // The caller may now read this slot's timestamps and write its mapped draw buffer.
    return true;
}

void cleanup_render_state(RenderState *rs) {
    if (rs->r_ctx)
        check_vkresult(vkDeviceWaitIdle(rs->r_ctx->ctx->device), SCOPE_GFX_COMMAND_BUFFER, "Drain render state");
    *rs = (RenderState){0};
}

void cleanup_render_context(RenderContext *rc) {
    const GraphicsContext *ctx = rc->ctx;
    check_vkresult(vkDeviceWaitIdle(ctx->device), SCOPE_GFX_COMMAND_BUFFER, "Drain frame slots");
    for (u32 i = 0; i < rc->frames_in_flight; i++) {
        vkDestroyCommandPool(ctx->device, rc->slots[i].pool, NULL);
        vkDestroyFence(ctx->device, rc->slots[i].fence, NULL);
        vkDestroySemaphore(ctx->device, rc->slots[i].image_available, NULL);
    }
    *rc = (RenderContext){0};
}
