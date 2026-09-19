#pragma once

#include <lib/grim/gfx/internal_graphics.h>

VkShaderModule read_shader(rop(rw Arena) arena, rop(ro GraphicsContext) ctx, rop(ro char) path) {
    FILE* shader_file = fopen(path, "r");
    if (shader_file == NULL) {
        CRITICAL_LOG(SCOPE_GFX_SHADER, "Failed to open shader file. Does this file exist?");
        exit(1);
    }

    const u32 BUFFER_SIZE      = 1024 * 1024 * 256;
    char*     read_buffer      = arena_alloc_align(arena, sizeof(u32), BUFFER_SIZE);
    u64       read_bytes_count = fread(read_buffer, 1, BUFFER_SIZE, shader_file);
    if (feof(shader_file)) {
        INFO_LOG(SCOPE_GFX_SHADER, "Successfully read %lu bytes from shader %s", read_bytes_count, path);
    } else if (ferror(shader_file)) {
        CRITICAL_LOG(SCOPE_GFX_SHADER, "An error occurred reading the specified shader.");
        exit(1);
    } else {
        CRITICAL_LOG(SCOPE_GFX_SHADER,
                     "Shader code is too big for a buffer size of 256MB. Either decrease the size"
                     "of the shader or increase the buffer size.");
        exit(1);
    }

    fclose(shader_file);

    VkShaderModule shader;

    VkShaderModuleCreateInfo create_info = {0};
    create_info.sType                    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize                 = read_bytes_count;
    create_info.pCode                    = (const u32*)read_buffer;

    VkResult result = vkCreateShaderModule(ctx->device, &create_info, NULL, &shader);
    check_vkresult(result, SCOPE_GFX_SHADER, "Failed to create shader module.");

    return shader;
}

void cleanup_shader(rop(ro GraphicsContext) ctx, rop(rw VkShaderModule) shader) {
    //
    vkDestroyShaderModule(ctx->device, *shader, NULL);
    *shader = VK_NULL_HANDLE;
}
