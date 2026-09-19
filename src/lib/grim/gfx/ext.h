#pragma once

#include <lib/grim/gfx/internal_graphics.h>

void init_ext_func_ptrs(VkInstance instance) {
    vk_func_ptrs._vkCmdDrawMeshTasksIndirectCountEXT =
        (PFN_vkCmdDrawMeshTasksIndirectCountEXT)vkGetInstanceProcAddr(instance, "vkCmdDrawMeshTasksIndirectCountEXT");
    if (unlikely(vk_func_ptrs._vkCmdDrawMeshTasksIndirectCountEXT == NULL)) {
        CRITICAL_LOG(SCOPE_GFX_EXT, "Failed to load create vkCmdDrawMeshTasksIndirectCountEXT fn_ptr.");
        exit(1);
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectCountEXT(VkCommandBuffer commandBuffer,
                                                              VkBuffer        buffer,
                                                              VkDeviceSize    offset,
                                                              VkBuffer        countBuffer,
                                                              VkDeviceSize    countBufferOffset,
                                                              uint32_t        maxDrawCount,
                                                              uint32_t        stride) {
    if (unlikely(vk_func_ptrs._vkCmdDrawMeshTasksIndirectCountEXT == NULL)) {
        CRITICAL_LOG(SCOPE_GFX_EXT,
                     "_vkCmdDrawMeshTasksIndirectCountEXT was NULL, did you forget to call init_ext_func_ptrs?");
        exit(1);
    }

    return vk_func_ptrs._vkCmdDrawMeshTasksIndirectCountEXT(
        commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}
