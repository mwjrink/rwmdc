#pragma once

VkShaderModule read_shader(Arena *arena, const GraphicsContext *ctx, const char *path) {
    (void)arena;
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "Unable to open shader %s\n", path);
        exit(EXIT_FAILURE);
    }
    long length = ftell(file);
    if (length <= 0 || length % 4 || fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Invalid SPIR-V file %s\n", path);
        fclose(file);
        exit(EXIT_FAILURE);
    }
    u32 *code = graphics_alloc((size_t)length / 4, sizeof(u32));
    size_t bytes = fread(code, 1, (size_t)length, file);
    fclose(file);
    if (bytes != (size_t)length) {
        fprintf(stderr, "Unable to read shader %s\n", path);
        free(code);
        exit(EXIT_FAILURE);
    }
    VkShaderModule module;
    VkShaderModuleCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = (size_t)length, .pCode = code};
    VkResult result = vkCreateShaderModule(ctx->device, &info, NULL, &module);
    free(code);
    check_vkresult(result, SCOPE_GFX_SHADER, "Create shader module");
    return module;
}
