#pragma once

#include <lib/grim/gfx/internal_graphics.h>

u32 pick_memory_type(rop(ro GraphicsContext) ctx, u32 memory_type_bits, VkMemoryPropertyFlags mem_property_flags) {
    // TODO can I cache this?
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(ctx->physical_device.handle, &mem_properties);

    for (u32 idx = 0; idx < mem_properties.memoryTypeCount; idx++) {
        if (memory_type_bits & (1 << idx)) {
            if ((mem_properties.memoryTypes[idx].propertyFlags & mem_property_flags) == mem_property_flags) {
                return idx;
            }
        }
    }

    return u32_MAX;
}

VkBuffer buffer_create_handle(rop(ro GraphicsContext) ctx, u64 size, VkBufferUsageFlags usage) {
    VkBufferCreateInfo create_info = {0};
    create_info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create_info.size               = size;
    create_info.usage              = usage;

    // TODO sparse binding sounds cool for alloc fragmentation...
    // create_info.flags = ;

    VkBuffer handle;
    // TODO maybe use VMA
    // VmaAllocation allocation;
    {
        // VkResult result = vmaCreateBuffer(ctx->allocator, &create_info, &alloc_info, &handle, &allocation, NULL);
        VkResult result = vkCreateBuffer(ctx->device, &create_info, NULL, &handle);
        check_vkresult(result, SCOPE_GFX_BUFFER, "Failed to create buffer!");
    }

    return handle;
}

VkDeviceMemory buffer_alloc_memory(rop(ro GraphicsContext) ctx, VkBuffer handle, VkMemoryPropertyFlags properties) {
    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(ctx->device, handle, &mem_requirements);
    u32 mem_type_idx = pick_memory_type(ctx, mem_requirements.memoryTypeBits, properties);

    if (mem_type_idx == u32_MAX) {
        WARNING_LOG(SCOPE_GFX_BUFFER, "Failed to find a suitable memory type for buffer.");
        return VK_NULL_HANDLE;
    }

    // TODO do I not just always want this flag on?
    VkMemoryAllocateFlagsInfo flags_info = {0};
    flags_info.sType                     = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flags_info.flags                     = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize       = mem_requirements.size;
    alloc_info.memoryTypeIndex      = mem_type_idx;
    alloc_info.pNext                = &flags_info;

    VkDeviceMemory memory_handle;
    {
        VkResult result = vkAllocateMemory(ctx->device, &alloc_info, NULL, &memory_handle);
        check_vkresult(result, SCOPE_GFX_BUFFER, "Failed to allocate buffer memory.");
    }

    return memory_handle;
}

void buffer_copy(rop(rw Arena) arena, rop(ro GraphicsContext) ctx, Buffer dst, Buffer src, u64 size, u64 offset) {
    // TODO make like 5 of these and rotate between/check if they are being used
    CommandPool*   pool           = &ctx->queue_families.families[ctx->queue_families.transfer_idx].command_pool;
    CommandBuffer* command_buffer = create_command_buffers(arena, ctx, pool, 1);
    command_buffer_begin_record(ctx, command_buffer, true);

    VkBufferCopy copy_region = {0};
    copy_region.srcOffset    = 0;
    copy_region.dstOffset    = 0;
    copy_region.size         = size;
    // TODO for batching, pass multiple regions here
    vkCmdCopyBuffer(command_buffer->handle, src.handle, dst.handle, 1, &copy_region);

    command_buffer_end_record(ctx, command_buffer);

    VkSubmitInfo submit_info       = {0};
    submit_info.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers    = &command_buffer->handle;

    vkQueueSubmit(ctx->queue_families.queues[ctx->queue_families.transfer_idx], 1, &submit_info, VK_NULL_HANDLE);
    // TODO no blocking ever!
    vkQueueWaitIdle(ctx->queue_families.queues[ctx->queue_families.transfer_idx]);

    vkFreeCommandBuffers(ctx->device, pool->handle, 1, &command_buffer->handle);
}

Buffer buffer_create(rop(ro GraphicsContext) ctx,
                     u64                   size,
                     VkBufferUsageFlags    usage,
                     VkMemoryPropertyFlags properties) {
    VkBuffer handle = buffer_create_handle(ctx, size, usage);

    VkDeviceMemory memory_handle = buffer_alloc_memory(ctx, handle, properties);

    if (memory_handle == VK_NULL_HANDLE) {
        CRITICAL_LOG(SCOPE_GFX_BUFFER, "Failed to find memory for buffer.");
        exit(1);
    }

    vkBindBufferMemory(ctx->device, handle, memory_handle, 0);

    VkDeviceAddress address = 0;
    if ((usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0) {
        VkBufferDeviceAddressInfo address_info = {0};
        address_info.sType                     = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        address_info.buffer                    = handle;
        address                                = vkGetBufferDeviceAddress(ctx->device, &address_info);
    }

    return (Buffer){
        .handle  = handle,
        .size    = size,
        .memory  = memory_handle,
        .address = address,
        // .view   = VK_NULL_HANDLE,
        // .align = align,
    };
}

void buffer_copy_with_staging(
    rop(rw Arena) arena, rop(ro GraphicsContext) ctx, Buffer dst_buffer, void* src, u64 size) {

    /* https://docs.vulkan.org/guide/latest/synchronization_examples.html#_transfer_dependencies
     * Unified Memory:
    // Data and size of that data
    const uint32_t vertexDataSize = ... ;
    const void* pData = ... ;

    // Create the vertex buffer
    VkBufferCreateInfo vertexCreateInfo = {
        ...
        .size = vertexDataSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        ... };

    VkBuffer vertexBuffer;
    vkCreateBuffer(device, &vertexCreateInfo, NULL, &vertexBuffer);

    ...

    // Allocate and memory bind memory for this buffer.
    // It should use a memory type that includes HOST_VISIBLE, and ideally also
    // DEVICE_LOCAL if available.
    // Use the example code documented in the description of
    // VkPhysicalDeviceMemoryProperties:
    // https://www.khronos.org/registry/vulkan/specs/latest/man/html/VkPhysicalDeviceMemoryProperties.html

    ...

    // Map the vertex buffer

    void* vertexData;

    vkMapMemory(
        ...
        vertexMemory,
        vertexMemoryOffset,
        vertexDataSize,
        0,
        &vertexData);

    // Write data directly into the mapped pointer
    fread(vertexData, vertexDataSize, 1, vertexFile);

    // Flush the memory range
    // If the memory type of vertexMemory includes VK_MEMORY_PROPERTY_HOST_COHERENT, skip this step

    // Align to the VkPhysicalDeviceProperties::nonCoherentAtomSize
    uint32_t alignedSize = (vertexDataSize-1) - ((vertexDataSize-1) % nonCoherentAtomSize) + nonCoherentAtomSize;

    // Setup the range
    VkMappedMemoryRange vertexRange = {
        ...
        .memory = vertexMemory,
        .offset = vertexMemoryOffset,
        .size   = alignedSize};

    // Flush the range
    vkFlushMappedMemoryRanges(device, 1, &vertexRange);

    // You may want to skip this if you're going to modify the
    // data again
    vkUnmapMemory(device, vertexMemory);
     */

    // TODO reuse one transfer buffer unless it's too small, then make a new one.
    // Probably don't recreate this every time we need to transfer
    // ALSO batch transfers in sections of this buffer?
    Buffer staging_buffer = buffer_create(ctx,
                                          size,
                                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    buffer_host_copy(ctx, &staging_buffer, src, size, 0);

    buffer_copy(arena, ctx, dst_buffer, staging_buffer, size, 0);

    cleanup_buffer(ctx, &staging_buffer);
}

void* buffer_map(rop(ro GraphicsContext) ctx, rop(rw Buffer) buffer) {
    // vkMapMemory2;
    VERBOSE_LOG(SCOPE_GFX_BUFFER,
                "Mapping memory:\n  - handle: %p\n  - memory: %p\n  - size: %lu\n  - offset: %lu",
                buffer->handle,
                buffer->memory,
                buffer->size,
                0lu);
    // TODO a special logger macro/function for auto formatting
    // stuff like this, use generic macro?
    vkMapMemory(ctx->device, buffer->memory, 0, buffer->size, 0, &buffer->mapped);
    return buffer->mapped;
}

void buffer_unmap(rop(ro GraphicsContext) ctx, rop(rw Buffer) buffer) {
    vkUnmapMemory(ctx->device, buffer->memory);
    buffer->mapped = NULL;
}

void buffer_host_copy(rop(ro GraphicsContext) ctx, rop(ro Buffer) buffer, void* src, u64 byte_len, u64 offset) {
    if (buffer->mapped != NULL) {
        memcpy(buffer->mapped, src, byte_len);
    } else {
        void* data = NULL;
        VERBOSE_LOG(SCOPE_GFX_BUFFER,
                    "Mapping memory:\n  - handle: %p\n  - memory: %p\n  - size: %lu\n  - offset: %lu",
                    buffer->handle,
                    buffer->memory,
                    byte_len,
                    offset);
        vkMapMemory(ctx->device, buffer->memory, offset, byte_len, 0, &data);
        memcpy(data, src, byte_len);
        vkUnmapMemory(ctx->device, buffer->memory);
    }
}

void cleanup_buffer(rop(ro GraphicsContext) ctx, rop(rw Buffer) buffer) {
    // if (buffer->view != VK_NULL_HANDLE) {
    //     vkDestroyBufferView(ctx->device, buffer->view, NULL);
    //     buffer->view = VK_NULL_HANDLE;
    // }
    if (buffer->mapped != NULL) {
        vkUnmapMemory(ctx->device, buffer->memory);
    }

    vkDestroyBuffer(ctx->device, buffer->handle, NULL);
    buffer->handle = VK_NULL_HANDLE;

    vkFreeMemory(ctx->device, buffer->memory, NULL);
    buffer->memory = VK_NULL_HANDLE;
}
