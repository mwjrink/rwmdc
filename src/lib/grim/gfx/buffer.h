#pragma once

static u32 pick_memory_type(const GraphicsContext *ctx, u32 bits, VkMemoryPropertyFlags required) {
    // Host-visible buffers benefit from device-local memory on UMA and resizable BAR.
    VkMemoryPropertyFlags preferred = required;
    if (required & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        required &= ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        preferred |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
    for (u32 pass = 0; pass < 2; pass++) {
        VkMemoryPropertyFlags flags = pass ? required : preferred;
        for (u32 i = 0; i < ctx->memory_properties.memoryTypeCount; i++)
            if ((bits & (1u << i)) && (ctx->memory_properties.memoryTypes[i].propertyFlags & flags) == flags)
                return i;
    }
    fprintf(stderr, "No compatible Vulkan memory type for flags 0x%x\n", required);
    exit(EXIT_FAILURE);
}

Buffer buffer_create(const GraphicsContext *ctx, u64 size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
    Buffer buffer = {.size = size};
    VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size,
        .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    check_vkresult(vkCreateBuffer(ctx->device, &info, NULL, &buffer.handle), SCOPE_GFX_BUFFER, "Create buffer");
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(ctx->device, buffer.handle, &requirements);
    u32 type = pick_memory_type(ctx, requirements.memoryTypeBits, properties);
    buffer.properties = ctx->memory_properties.memoryTypes[type].propertyFlags;
    buffer.allocation_size = requirements.size;
    VkMemoryAllocateFlagsInfo flags = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? &flags : NULL,
        .allocationSize = requirements.size, .memoryTypeIndex = type};
    check_vkresult(vkAllocateMemory(ctx->device, &allocation, NULL, &buffer.memory), SCOPE_GFX_BUFFER, "Allocate buffer memory");
    check_vkresult(vkBindBufferMemory(ctx->device, buffer.handle, buffer.memory, 0), SCOPE_GFX_BUFFER, "Bind buffer memory");
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo address = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = buffer.handle};
        buffer.address = vkGetBufferDeviceAddress(ctx->device, &address);
        if (!buffer.address) {
            fprintf(stderr, "Vulkan bufferDeviceAddress returned a null address\n");
            exit(EXIT_FAILURE);
        }
    }
    if (buffer.properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        check_vkresult(vkMapMemory(ctx->device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped), SCOPE_GFX_BUFFER, "Map buffer memory");
    return buffer;
}

void buffer_host_copy(const GraphicsContext *ctx, Buffer *buffer, const void *data, u64 bytes, u64 offset) {
    if (!buffer->mapped || offset > buffer->size || bytes > buffer->size - offset) {
        fprintf(stderr, "Invalid host buffer write\n");
        exit(EXIT_FAILURE);
    }
    if (!bytes) return;
    memcpy((u8 *)buffer->mapped + offset, data, bytes);
    if (!(buffer->properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        u64 atom = ctx->properties.limits.nonCoherentAtomSize;
        u64 start = offset - offset % atom;
        u64 end = (offset + bytes + atom - 1) / atom * atom;
        VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = buffer->memory, .offset = start,
            .size = end >= buffer->allocation_size ? VK_WHOLE_SIZE : end - start};
        check_vkresult(vkFlushMappedMemoryRanges(ctx->device, 1, &range), SCOPE_GFX_BUFFER, "Flush mapped buffer");
    }
}

void cleanup_buffer(const GraphicsContext *ctx, Buffer *buffer) {
    if (buffer->mapped) vkUnmapMemory(ctx->device, buffer->memory);
    if (buffer->handle) vkDestroyBuffer(ctx->device, buffer->handle, NULL);
    if (buffer->memory) vkFreeMemory(ctx->device, buffer->memory, NULL);
    *buffer = (Buffer){0};
}

