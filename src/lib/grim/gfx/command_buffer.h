#pragma once

#include <lib/grim/gfx/internal_graphics.h>

CommandPool create_transient_command_pool(rop(ro GraphicsContext) ctx, ro u32 family_idx) {
    VkCommandPoolCreateInfo create_info = {0};
    create_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    create_info.flags                   = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    create_info.queueFamilyIndex        = family_idx;

    VkCommandPool handle = {0};
    VkResult      result = vkCreateCommandPool(ctx->device, &create_info, NULL, &handle);
    check_vkresult(result, SCOPE_GFX_COMMAND_BUFFER, "Failed to create Command Pool!");

    CommandPool command_pool = {0};

    command_pool.handle = handle;

    return command_pool;
}

CommandPool create_command_pool(rop(ro GraphicsContext) ctx, ro u32 family_idx) {
    VkCommandPoolCreateInfo create_info = {0};
    create_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // TODO find a way to not use this flag?
    create_info.queueFamilyIndex = family_idx;

    VkCommandPool handle = {0};
    VkResult      result = vkCreateCommandPool(ctx->device, &create_info, NULL, &handle);
    check_vkresult(result, SCOPE_GFX_COMMAND_BUFFER, "Failed to create Command Pool!");

    CommandPool command_pool = {0};

    command_pool.handle = handle;

    return command_pool;
}

CommandBuffer* create_command_buffers(rop(rw Arena) arena,
                                      rop(ro GraphicsContext) ctx,
                                      rop(rw CommandPool) pool,
                                      u32 buffer_count) {
    VkCommandBufferAllocateInfo create_info = {0};
    create_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    create_info.commandPool                 = pool->handle;
    create_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    create_info.commandBufferCount          = buffer_count;

    CommandBuffer* command_buffers = arena_alloc_align(arena, sizeof(void*), sizeof(CommandBuffer) * buffer_count);

    arena_ckpt(arena);
    {
        VkCommandBuffer* handles =
            arena_alloc_align(arena, sizeof(VkCommandBuffer), sizeof(VkCommandBuffer) * buffer_count);
        VkResult result = vkAllocateCommandBuffers(ctx->device, &create_info, handles);
        check_vkresult(result, SCOPE_GFX_COMMAND_BUFFER, "Failed to create Command Buffer!");

        for (u32 idx = 0; idx < buffer_count; idx++) {
            command_buffers[idx].handle = handles[idx];
        }
    }
    arena_pop(arena);

    pool->command_buffers      = command_buffers;
    pool->command_buffer_count = buffer_count;

    return command_buffers;
}

void command_buffer_begin_record(rop(ro GraphicsContext) ctx, rop(ro CommandBuffer) command_buffer, u32 one_time) {
    VkCommandBufferBeginInfo begin_info = {0};
    begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pInheritanceInfo         = NULL;

    if (one_time == true) {
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    } else {
        begin_info.flags = 0;
    }

    VkResult result = vkBeginCommandBuffer(command_buffer->handle, &begin_info);
    check_vkresult(result, SCOPE_GFX_COMMAND_BUFFER, "Failed to begin Command Buffer recording.");
}

void command_buffer_end_record(rop(ro GraphicsContext) ctx, rop(ro CommandBuffer) command_buffer) {
    VkResult result = vkEndCommandBuffer(command_buffer->handle);
    check_vkresult(result, SCOPE_GFX_COMMAND_BUFFER, "Error occurred recording command buffer.");
}

void reset_command_pool(rop(ro GraphicsContext) ctx, rop(rw CommandPool) command_pool) {
    exit(100); // TODO unimplemented

    // vkResetCommandPool(ctx->device, command_pool->handle, );
}

void cleanup_command_buffer(rop(ro GraphicsContext) ctx, rop(rw CommandPool) command_pool) {
    exit(100); // TODO unimplemented

    // vkResetCommandBuffer(ctx->device, command_pool->handle, );
}
void cleanup_command_pool(rop(ro GraphicsContext) ctx, rop(rw CommandPool) command_pool) {
    // vkResetCommandBuffer();

    vkDestroyCommandPool(ctx->device, command_pool->handle, NULL);
    command_pool->handle = VK_NULL_HANDLE;
}
