#pragma once

#include <lib/grim/logger.h>
#include <lib/grim/gfx/graphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_vkresult(VkResult result, LogScope scope, const char *message) {
    if (result != VK_SUCCESS) {
        CRITICAL_LOG(scope, "%s (VkResult %d)", message, result);
        exit(EXIT_FAILURE);
    }
}

static void *graphics_alloc(size_t count, size_t size) {
    void *memory = calloc(count, size);
    if (!memory) {
        fprintf(stderr, "Unable to allocate graphics metadata\n");
        exit(EXIT_FAILURE);
    }
    return memory;
}

#include <lib/grim/gfx/vkutils.h>
#include <lib/grim/gfx/buffer.h>
#include <lib/grim/gfx/image.h>
#include <lib/grim/gfx/shader.h>
#include <lib/grim/gfx/pipeline.h>
#include <lib/grim/gfx/render_target.h>
#include <lib/grim/gfx/context.h>
#include <lib/grim/gfx/swapchain.h>
#include <lib/grim/gfx/rendering.h>
#include <lib/grim/gfx/enqueue.h>
