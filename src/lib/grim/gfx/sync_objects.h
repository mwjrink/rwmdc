#pragma once

#include <lib/grim/gfx/internal_graphics.h>

void recreate_sync_objects_semaphores(rop(ro GraphicsContext) ctx, rop(rw SyncObjects) sync_object) {
    // TODO one per swapchain image
    {
        VkSemaphoreCreateInfo create_info = {0};
        create_info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkResult result = vkCreateSemaphore(ctx->device, &create_info, NULL, &(sync_object->signal_on_image_available));
        check_vkresult(result, SCOPE_GFX_INIT, "Failed to create Semaphore.");
    }

    // TODO one per frame in flight
    {
        VkSemaphoreCreateInfo create_info = {0};
        create_info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkResult result = vkCreateSemaphore(ctx->device, &create_info, NULL, &(sync_object->signal_on_render_finish));
        check_vkresult(result, SCOPE_GFX_INIT, "Failed to create Semaphore.");
    }
}

void recreate_present_complete(rop(ro GraphicsContext) ctx, rop(rw SyncObjects) sync_object) {
    {
        VkFenceCreateInfo create_info = {0};
        create_info.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        create_info.flags             = VK_FENCE_CREATE_SIGNALED_BIT;
        VkResult result = vkCreateFence(ctx->device, &create_info, NULL, &(sync_object->open_on_present_complete));
        check_vkresult(result, SCOPE_GFX_INIT, "Failed to create Fence.");
    }
}

SyncObjects create_sync_objects(rop(ro GraphicsContext) ctx) {
    SyncObjects sync_object = {0};

    recreate_sync_objects_semaphores(ctx, &sync_object);

    // TODO one per frame in flight
    {
        VkFenceCreateInfo create_info = {0};
        create_info.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        create_info.flags             = VK_FENCE_CREATE_SIGNALED_BIT;
        VkResult result = vkCreateFence(ctx->device, &create_info, NULL, &(sync_object.open_on_render_finish));
        check_vkresult(result, SCOPE_GFX_INIT, "Failed to create Fence.");
    }

    // one per swapchain image?
    recreate_present_complete(ctx, &sync_object);

    return sync_object;
}

void cleanup_sync_objects(rop(ro GraphicsContext) ctx, rop(rw SyncObjects) sync_objects) {
    //
    vkDestroySemaphore(ctx->device, sync_objects->signal_on_image_available, NULL);
    sync_objects->signal_on_image_available = VK_NULL_HANDLE;

    vkDestroySemaphore(ctx->device, sync_objects->signal_on_render_finish, NULL);
    sync_objects->signal_on_render_finish = VK_NULL_HANDLE;

    vkDestroyFence(ctx->device, sync_objects->open_on_render_finish, NULL);
    sync_objects->open_on_render_finish = VK_NULL_HANDLE;

    vkDestroyFence(ctx->device, sync_objects->open_on_present_complete, NULL);
    sync_objects->open_on_present_complete = VK_NULL_HANDLE;
}
