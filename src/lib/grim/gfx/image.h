#pragma once

#include <lib/grim/gfx/internal_graphics.h>

// void set_stbi_sucks_arena_ctx(void* arena);

Image create_image(rop(ro GraphicsContext) ctx,
                   u32                      width,
                   u32                      height,
                   VkFormat                 format,
                   VkImageTiling            tiling,
                   VkImageUsageFlagBits     usage,
                   VkMemoryPropertyFlagBits mem_properties) {
    Image image  = {0};
    image.format = format;

    VkImageCreateInfo create_info = {0};
    create_info.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create_info.imageType         = VK_IMAGE_TYPE_2D;
    create_info.extent.width      = width;
    create_info.extent.height     = height;
    create_info.extent.depth      = 1;
    create_info.mipLevels         = 1;
    create_info.arrayLayers       = 1;
    create_info.format            = format;
    create_info.tiling            = tiling;
    create_info.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
    create_info.usage             = usage;
    create_info.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;
    create_info.samples           = VK_SAMPLE_COUNT_1_BIT;
    create_info.flags             = 0;

    {
        VkResult result = vkCreateImage(ctx->device, &create_info, NULL, &image.handle);
        check_vkresult(result, SCOPE_GFX_IMAGE, "Failed to create image.");
    }

    VkMemoryRequirements mem_requirements;
    vkGetImageMemoryRequirements(ctx->device, image.handle, &mem_requirements);

    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize       = mem_requirements.size;
    alloc_info.memoryTypeIndex =
        pick_memory_type(ctx, mem_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    {
        VkResult result = vkAllocateMemory(ctx->device, &alloc_info, NULL, &image.memory);
        check_vkresult(result, SCOPE_GFX_IMAGE, "Failed to allocate memory for image.");
    }

    {
        VkResult result = vkBindImageMemory(ctx->device, image.handle, image.memory, 0);
        check_vkresult(result, SCOPE_GFX_IMAGE, "Failed to bind image memory.");
    }

    return image;
}

// TODO this should be scratch space. There should be a way to turn a scratch arena into an arena
// This comes after we refactor checkpoints, then the structures will be identical, right?
// TODO make a batch version of this function that does all the transitioning and uploading in batches
Image image_load_texture_mem(rop(rw Arena) arena, rop(ro GraphicsContext) ctx, rop(ro void) data, u64 byte_size) {
    u64 start_stbi_load = time_start();

    arena_ckpt(arena);
    // set_stbi_sucks_arena_ctx(arena);

    int tex_width, tex_height, tex_channels;
    // stbi_uc* pixels =
    //     stbi_load_from_memory(data, (u32)byte_size, &tex_width, &tex_height, &tex_channels, STBI_rgb_alpha);
    // if (pixels == NULL) {
    //     const char* failure_reason = stbi_failure_reason();
    //     CRITICAL_LOG(SCOPE_GFX_IMAGE, "Failed to load image! %s", failure_reason);
    //     exit(1);
    // }

    // set_stbi_sucks_arena_ctx(NULL);

    DEBUG_LOG(SCOPE_GFX_IMAGE, "start_stbi_load");
    time_end(SCOPE_GFX_IMAGE, start_stbi_load);

    u64 start_host_copy = time_start();

    VkDeviceSize image_size = tex_width * tex_height * 4;
    Buffer       staging    = buffer_create(ctx,
                                            image_size,
                                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    // buffer_host_copy(ctx, &staging, pixels, staging.size, 0);

    DEBUG_LOG(SCOPE_GFX_IMAGE, "start_host_copy");
    time_end(SCOPE_GFX_IMAGE, start_host_copy);

    // TODO stbi
    // - decode from memory or through FILE (define STBI_NO_STDIO to remove code)
    // You can #define STBI_ASSERT(x) before the #include to avoid using assert.h.
    // And #define STBI_MALLOC, STBI_REALLOC, and STBI_FREE to avoid using malloc,realloc,free

    u64 image_free = time_start();
    // stbi_image_free(pixels);
    DEBUG_LOG(SCOPE_GFX_IMAGE, "image_free");
    time_end(SCOPE_GFX_IMAGE, image_free);

    u64 create_image_start = time_start();

    Image image = create_image(ctx,
                               tex_width,
                               tex_height,
                               VK_FORMAT_B8G8R8A8_SRGB,
                               VK_IMAGE_TILING_OPTIMAL,
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    DEBUG_LOG(SCOPE_GFX_IMAGE, "create_image_start");
    time_end(SCOPE_GFX_IMAGE, create_image_start);

    u64 pool_buffer_start = time_start();

    CommandPool*   pool           = &ctx->queue_families.families[ctx->queue_families.transfer_idx].command_pool;
    CommandBuffer* command_buffer = create_command_buffers(arena, ctx, pool, 1);
    command_buffer_begin_record(ctx, command_buffer, true);

    cmd_image_transition(
        ctx, command_buffer, image.handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region = {0};
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;

    region.imageOffset.x      = 0;
    region.imageOffset.y      = 0;
    region.imageOffset.z      = 0;
    region.imageExtent.width  = tex_width;
    region.imageExtent.height = tex_height;
    region.imageExtent.depth  = 1;

    vkCmdCopyBufferToImage(
        command_buffer->handle, staging.handle, image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    cmd_image_transition(ctx,
                         command_buffer,
                         image.handle,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    command_buffer_end_record(ctx, command_buffer);

    VkSubmitInfo submit_info       = {0};
    submit_info.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers    = &command_buffer->handle;

    vkQueueSubmit(ctx->queue_families.queues[ctx->queue_families.transfer_idx], 1, &submit_info, VK_NULL_HANDLE);

    // TODO no blocking ever!
    vkQueueWaitIdle(ctx->queue_families.queues[ctx->queue_families.transfer_idx]);

    vkFreeCommandBuffers(ctx->device, pool->handle, 1, &command_buffer->handle);

    arena_pop(arena);

    image.view = create_image_view(ctx, image.handle, VK_FORMAT_B8G8R8A8_SRGB);

    cleanup_buffer(ctx, &staging);

    DEBUG_LOG(SCOPE_GFX_IMAGE, "pool_buffer_start");
    time_end(SCOPE_GFX_IMAGE, pool_buffer_start);

    // TODO 3d image for voxel volume
    // witcher 4 showcase foliage?

    return image;
}

Image image_load_texture(rop(rw Arena) arena, rop(ro GraphicsContext) ctx, const char* path) {

    arena_ckpt(arena);

    // set_stbi_sucks_arena_ctx(arena);

    int   tex_width, tex_height, tex_channels;
    // TODO can I give it a buffer to load into?
    // Load from mmap'd file:
    // stbi_load_from_memory
    void* pixels = NULL;
    // stbi_uc* pixels = stbi_load(path, &tex_width, &tex_height, &tex_channels, STBI_rgb_alpha);
    // if (pixels == NULL) {
    //     const char* failure_reason = stbi_failure_reason();
    //     CRITICAL_LOG(SCOPE_GFX_IMAGE, "Failed to load image! %s", failure_reason);
    //     exit(1);
    // }

    // set_stbi_sucks_arena_ctx(NULL);

    VkDeviceSize image_size = tex_width * tex_height * 4;
    Buffer       staging    = buffer_create(ctx,
                                            image_size,
                                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    buffer_host_copy(ctx, &staging, pixels, staging.size, 0);

    // stbi_image_free(pixels);

    Image image = create_image(ctx,
                               tex_width,
                               tex_height,
                               VK_FORMAT_B8G8R8A8_SRGB,
                               VK_IMAGE_TILING_OPTIMAL,
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    CommandPool*   pool           = &ctx->queue_families.families[ctx->queue_families.transfer_idx].command_pool;
    CommandBuffer* command_buffer = create_command_buffers(arena, ctx, pool, 1);
    command_buffer_begin_record(ctx, command_buffer, true);

    cmd_image_transition(
        ctx, command_buffer, image.handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region = {0};
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;

    region.imageOffset.x      = 0;
    region.imageOffset.y      = 0;
    region.imageOffset.z      = 0;
    region.imageExtent.width  = tex_width;
    region.imageExtent.height = tex_height;
    region.imageExtent.depth  = 1;

    vkCmdCopyBufferToImage(
        command_buffer->handle, staging.handle, image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    cmd_image_transition(ctx,
                         command_buffer,
                         image.handle,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    command_buffer_end_record(ctx, command_buffer);

    VkSubmitInfo submit_info       = {0};
    submit_info.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers    = &command_buffer->handle;

    vkQueueSubmit(ctx->queue_families.queues[ctx->queue_families.transfer_idx], 1, &submit_info, VK_NULL_HANDLE);

    // TODO no blocking ever!
    vkQueueWaitIdle(ctx->queue_families.queues[ctx->queue_families.transfer_idx]);

    vkFreeCommandBuffers(ctx->device, pool->handle, 1, &command_buffer->handle);

    arena_pop(arena);

    image.view = create_image_view(ctx, image.handle, VK_FORMAT_B8G8R8A8_SRGB);

    cleanup_buffer(ctx, &staging);

    // TODO 3d image for voxel volume
    // witcher 4 showcase foliage?

    return image;
}

VkImageView create_image_view(rop(ro GraphicsContext) ctx, ro VkImage image, ro VkFormat format) {
    VkImageViewCreateInfo create_info = {0};
    create_info.sType                 = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image                 = image;
    create_info.viewType              = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format                = format;

    // TODO create a swizzle LUT? or just ensure format of images
    // before we load them/when we encode them on disk
    // if (format == VK_FORMAT_B8G8R8A8_SRGB) {
    //     create_info.components.r = VK_COMPONENT_SWIZZLE_B;
    //     create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    //     create_info.components.b = VK_COMPONENT_SWIZZLE_R;
    //     create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    // } else {
    create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    // }

    if (format_is_depth(format)) {
        create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    } else {
        create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    create_info.subresourceRange.baseMipLevel = 0;
    create_info.subresourceRange.levelCount   = 1;

    create_info.subresourceRange.baseArrayLayer = 0;
    create_info.subresourceRange.layerCount     = 1;

    VkImageView view;
    VkResult    result = vkCreateImageView(ctx->device, &create_info, NULL, &view);
    check_vkresult(result, SCOPE_GFX_IMAGE, "Failed to create image view.");

    return view;
}

VkSampler image_create_sampler(rop(ro GraphicsContext) ctx) {
    VkSamplerCreateInfo sampler_info = {0};
    sampler_info.sType               = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter           = VK_FILTER_LINEAR;
    sampler_info.minFilter           = VK_FILTER_LINEAR;

    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;

    // TODO this should be changeable in settings somewhere?
    sampler_info.anisotropyEnable = VK_TRUE;
    sampler_info.maxAnisotropy =
        ctx->physical_device.prop_feats->device_properties2.properties.limits.maxSamplerAnisotropy;
    sampler_info.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable           = VK_FALSE;
    sampler_info.compareOp               = VK_COMPARE_OP_ALWAYS;

    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.mipLodBias = 0.0f;
    sampler_info.minLod     = 0.0f;
    sampler_info.maxLod     = 0.0f;

    VkSampler texture_sampler;
    VkResult  result = vkCreateSampler(ctx->device, &sampler_info, NULL, &texture_sampler);
    check_vkresult(result, SCOPE_GFX_IMAGE, "Failed to create image sampler.");

    return texture_sampler;
}

void cleanup_image(rop(ro GraphicsContext) ctx, rop(rw Image) image) {
    vkDestroyImageView(ctx->device, image->view, NULL);
    image->view = VK_NULL_HANDLE;
    vkDestroyImage(ctx->device, image->handle, NULL);
    image->handle = VK_NULL_HANDLE;
    if (image->memory != VK_NULL_HANDLE) {
        vkFreeMemory(ctx->device, image->memory, NULL);
        image->memory = VK_NULL_HANDLE;
    }
}

void cleanup_image_view(rop(ro GraphicsContext) ctx, rop(rw Image) image) {
    vkDestroyImageView(ctx->device, image->view, NULL);
    image->view = VK_NULL_HANDLE;
}

void cleanup_image_sampler(rop(ro GraphicsContext) ctx, VkSampler sampler) {
    vkDestroySampler(ctx->device, sampler, NULL);
}
