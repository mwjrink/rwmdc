#pragma once

#include <lib/grim/gfx/internal_graphics.h>

void swapchain_get_images(rop(rw Arena) arena, rop(ro GraphicsContext) ctx, rop(rw RenderTarget) render_target) {
    u32 swapchain_image_count = 0;
    {
        VkResult result =
            vkGetSwapchainImagesKHR(ctx->device, render_target->swapchain.handle, &swapchain_image_count, NULL);
        check_vkresult(result, SCOPE_GFX_SWAPCHAIN, "Failed to get swapchain images count.");
    };

    Image* images = arena_alloc(arena, sizeof(Image) * swapchain_image_count);

    arena_ckpt(arena);
    VkImage* swapchain_images = arena_alloc(arena, sizeof(VkImage) * swapchain_image_count);
    {
        VkResult result = vkGetSwapchainImagesKHR(
            ctx->device, render_target->swapchain.handle, &swapchain_image_count, swapchain_images);
        check_vkresult(result, SCOPE_GFX_SWAPCHAIN, "Failed to get swapchain images.");
    };

    for (u32 idx = 0; idx < swapchain_image_count; idx++) {
        images[idx].handle = swapchain_images[idx];
        images[idx].view   = create_image_view(ctx, swapchain_images[idx], render_target->format);
    }

    arena_pop(arena);

    render_target->swapchain.image_count = swapchain_image_count;
    render_target->swapchain.images      = images;
}

SwapchainGarbage recreate_swapchain(rop(rw Arena) arena,
                                    rop(ro GraphicsContext) ctx,
                                    rop(rw RenderTarget) render_target) {

    // TODO we need to update this based on window size
    // render_target->extent = ;
    // TODO special case of 0 when minimized, something like this:
    // glfwGetFramebufferSize(window, &width, &height);
    // while (width == 0 || height == 0) {
    //     glfwGetFramebufferSize(window, &width, &height);
    //     glfwWaitEvents();
    // }

    VkSurfaceCapabilitiesKHR surface_capabilities;
    {
        VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            ctx->physical_device.handle, render_target->surface, &surface_capabilities);
        check_vkresult(result, SCOPE_GFX_SWAPCHAIN, "Failed to get surface capabilities.");
    }

    VkSwapchainCreateInfoKHR create_info = {0};
    create_info.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface                  = render_target->surface;
    // TODO these should change if settings are changed in the game: double buffering etc
    create_info.minImageCount            = render_target->swapchain.image_count;
    create_info.imageFormat              = render_target->format;
    create_info.imageColorSpace          = render_target->color_space;
    // TODO check if extent 0 at the top of the func and DO NOT recreate if it is, will fail
    if (surface_capabilities.currentExtent.width == u32_MAX) {
        create_info.imageExtent = render_target->extent;
    } else {
        create_info.imageExtent = surface_capabilities.currentExtent;
    }
    create_info.imageArrayLayers = 1;
    create_info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // TODO do this properly
    // uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};
    // if (indices.graphicsFamily != indices.presentFamily) {
    //     createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    //     createInfo.queueFamilyIndexCount = 2;
    //     createInfo.pQueueFamilyIndices = queueFamilyIndices;
    // } else {
    create_info.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
    // optional when exclusive
    create_info.queueFamilyIndexCount = 0;
    create_info.pQueueFamilyIndices   = NULL;
    // }

    create_info.preTransform   = surface_capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // For compositors with alpha windowing support
    // create_info.compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;

    // TODO these should change if settings are changed in the game: vsync
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    create_info.presentMode       = present_mode;
    create_info.clipped           = VK_TRUE;
    create_info.oldSwapchain      = render_target->swapchain.handle;

    VkSwapchainKHR swapchain;
    {
        VkResult result = vkCreateSwapchainKHR(ctx->device, &create_info, NULL, &swapchain);
        check_vkresult(result, SCOPE_GFX_SWAPCHAIN, "Failed to create swapchain.");
    }

    // TODO these should change if settings are changed in the game?
    // These are not changing here.
    // render_target->format = format;
    // render_target->color_space = color_space;
    assert(SCOPE_GFX_SWAPCHAIN, render_target->swapchain.image_count <= 3);
    SwapchainGarbage garbage = (SwapchainGarbage){
        .swapchain   = render_target->swapchain.handle,
        .garbage_len = render_target->swapchain.image_count,
    };

    for (u32 idx = 0; idx < garbage.garbage_len; idx++) {
        garbage.image_views[idx] = render_target->swapchain.images[idx].view;

        garbage.signal_on_render_finishes[idx]  = render_target->swapchain.sync_objects[idx].signal_on_render_finish;
        garbage.signal_on_image_availables[idx] = render_target->swapchain.sync_objects[idx].signal_on_image_available;
        garbage.open_on_present_complete[idx]   = render_target->swapchain.sync_objects[idx].open_on_present_complete;
    }

    arena_ckpt(arena);

    render_target->swapchain.handle  = swapchain;
    rop(rw VkImage) swapchain_images = arena_alloc(arena, sizeof(VkImage) * render_target->swapchain.image_count);
    {
        VkResult result =
            vkGetSwapchainImagesKHR(ctx->device, swapchain, &render_target->swapchain.image_count, swapchain_images);
        check_vkresult(result, SCOPE_GFX_SWAPCHAIN, "Failed to get swapchain images.");
    };

    for (u32 idx = 0; idx < render_target->swapchain.image_count; idx++) {
        recreate_sync_objects_semaphores(ctx, render_target->swapchain.sync_objects + idx);
        recreate_present_complete(ctx, render_target->swapchain.sync_objects + idx);
        render_target->swapchain.images[idx].handle = swapchain_images[idx];
        render_target->swapchain.images[idx].view =
            create_image_view(ctx, swapchain_images[idx], render_target->format);
    }

    arena_pop(arena);

    return garbage;
}

u8 check_dispose_ready(rop(ro GraphicsContext) ctx, rop(ro SwapchainGarbage) garbage) {
    VkResult result = vkWaitForFences(ctx->device, garbage->garbage_len, garbage->open_on_present_complete, VK_TRUE, 0);
    // TODO use fence status instead? Not sure if it matters since timeout 0 is a special case.
    // Can I even effectively performance test this?
    // vkGetFenceStatus(ctx->device, garbage->open_on_present_complete);
    return result == VK_SUCCESS;
}

void dispose_swapchain_garbage(rop(ro GraphicsContext) ctx, rop(rw SwapchainGarbage) garbage) {
    // we target 3 buffers always so unroll 3 here
#pragma unroll 3
    for (u32 idx = 0; idx < garbage->garbage_len; idx++) {
        vkDestroyImageView(ctx->device, garbage->image_views[idx], NULL);
        garbage->image_views[idx] = VK_NULL_HANDLE;

        vkDestroySemaphore(ctx->device, garbage->signal_on_render_finishes[idx], NULL);
        garbage->signal_on_render_finishes[idx] = VK_NULL_HANDLE;

        vkDestroySemaphore(ctx->device, garbage->signal_on_image_availables[idx], NULL);
        garbage->signal_on_image_availables[idx] = VK_NULL_HANDLE;

        vkDestroyFence(ctx->device, garbage->open_on_present_complete[idx], NULL);
        garbage->open_on_present_complete[idx] = VK_NULL_HANDLE;
    }
    vkDestroySwapchainKHR(ctx->device, garbage->swapchain, NULL);
    garbage->swapchain = VK_NULL_HANDLE;
}

void create_swapchain(rop(rw Arena) arena,
                      rop(ro GraphicsContext) ctx,
                      ro VkSurfaceKHR surface,
                      rop(rw RenderTarget) render_target) {
    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physical_device.handle, surface, &surface_capabilities);

    arena_ckpt(arena);

    u32                 format_count;
    VkSurfaceFormatKHR* formats = NULL;
    {
        {
            VkResult result =
                vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device.handle, surface, &format_count, NULL);
            check_vkresult(
                result, SCOPE_GFX_SWAPCHAIN, "Failed to queury supported physical device surface format count.");
        }

        formats = arena_alloc(arena, sizeof(VkSurfaceFormatKHR) * format_count);
        if (format_count != 0) {
            VkResult result =
                vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device.handle, surface, &format_count, formats);
            check_vkresult(result, SCOPE_GFX_SWAPCHAIN, "Failed to queury physical device surface formats.");
        }
    }

    u32               present_mode_count;
    VkPresentModeKHR* present_modes = NULL;
    {
        {
            VkResult result = vkGetPhysicalDeviceSurfacePresentModesKHR(
                ctx->physical_device.handle, surface, &present_mode_count, NULL);
            check_vkresult(
                result, SCOPE_GFX_SWAPCHAIN, "Failed to queury supported physical device surface format count.");
        }

        present_modes = arena_alloc(arena, sizeof(VkPresentModeKHR) * present_mode_count);
        if (present_mode_count != 0) {
            VkResult result = vkGetPhysicalDeviceSurfacePresentModesKHR(
                ctx->physical_device.handle, surface, &present_mode_count, present_modes);
            check_vkresult(
                result, SCOPE_GFX_SWAPCHAIN, "Failed to queury supported physical device surface format count.");
        }
    }

    INFO_LOG(SCOPE_GFX_SWAPCHAIN, "Supported Color Spaces: ");
    INFO_LOG(SCOPE_GFX_SWAPCHAIN, "Supported Surface Format: ");
    for (u32 idx = 0; idx < format_count; idx++) {
        const char* color_space    = colorspace_to_str(formats[idx].colorSpace);
        const char* surface_format = surface_format_to_str(formats[idx].format);
        INFO_LOG(SCOPE_GFX_SWAPCHAIN, "    %s & %s", surface_format, color_space);
    }
    INFO_LOG(SCOPE_GFX_SWAPCHAIN, "");

    INFO_LOG(SCOPE_GFX_SWAPCHAIN, "Supported Present Modes: ");
    for (u32 idx = 0; idx < present_mode_count; idx++) {
        const char* present_mode = present_mode_to_str(present_modes[idx]);
        INFO_LOG(SCOPE_GFX_SWAPCHAIN, "    %s", present_mode);
    }
    INFO_LOG(SCOPE_GFX_SWAPCHAIN, "");

    INFO_LOG(SCOPE_GFX_SWAPCHAIN,
             "Surface Extent: %u x %u",
             surface_capabilities.currentExtent.width,
             surface_capabilities.currentExtent.height);
    INFO_LOG(SCOPE_GFX_SWAPCHAIN,
             "Min Surface Extent: %u x %u",
             surface_capabilities.minImageExtent.width,
             surface_capabilities.minImageExtent.height);
    INFO_LOG(SCOPE_GFX_SWAPCHAIN,
             "Max Surface Extent: %u x %u",
             surface_capabilities.maxImageExtent.width,
             surface_capabilities.maxImageExtent.height);

    // TODO pick these properly
    // VkFormat         format       = VK_FORMAT_A2R10G10B10_UNORM_PACK32;
    VkFormat         format       = VK_FORMAT_B8G8R8A8_SRGB;
    // VK_FORMAT_B8G8R8A8_UNORM
    VkColorSpaceKHR  color_space  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    // Mailbox is what you probably want if it exists
    // VkPresentModeKHR present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    // We want 3 but are limited by driver, get closest supported value
    INFO_LOG(SCOPE_GFX_SWAPCHAIN,
             "We can have %u - %u images.",
             surface_capabilities.minImageCount,
             surface_capabilities.maxImageCount);
    u32 swapchain_image_count = clamp(surface_capabilities.minImageCount, 3, surface_capabilities.maxImageCount);

    VkSwapchainCreateInfoKHR create_info = {0};
    create_info.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface                  = surface;
    create_info.minImageCount            = swapchain_image_count;
    create_info.imageFormat              = format;
    create_info.imageColorSpace          = color_space;
    create_info.imageExtent              = render_target->extent;
    create_info.imageArrayLayers         = 1;
    create_info.imageUsage               = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // TODO do this properly
    // uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};
    // if (indices.graphicsFamily != indices.presentFamily) {
    //     createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    //     createInfo.queueFamilyIndexCount = 2;
    //     createInfo.pQueueFamilyIndices = queueFamilyIndices;
    // } else {
    create_info.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
    // optional when exclusive
    create_info.queueFamilyIndexCount = 0;
    create_info.pQueueFamilyIndices   = NULL;
    // }

    create_info.preTransform   = surface_capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // For compositors with alpha windowing support
    // create_info.compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;

    create_info.presentMode  = present_mode;
    create_info.clipped      = VK_TRUE;
    // TODO do you have to clean this up?
    create_info.oldSwapchain = render_target->swapchain.handle;

    VkSwapchainKHR swapchain;
    VkResult       result = vkCreateSwapchainKHR(ctx->device, &create_info, NULL, &swapchain);
    check_vkresult(result, SCOPE_GFX_SWAPCHAIN, "Failed to create swapchain.");

    render_target->format      = format;
    render_target->color_space = color_space;

    arena_pop(arena);

    SyncObjects* sync_objects = arena_alloc_align(arena, sizeof(void*), sizeof(SyncObjects) * swapchain_image_count);
    for (u32 idx = 0; idx < swapchain_image_count; idx++) {
        sync_objects[idx] = create_sync_objects(ctx);
    }

    Swapchain swapchain_wrapper    = {0};
    swapchain_wrapper.handle       = swapchain;
    swapchain_wrapper.sync_objects = sync_objects;
    render_target->swapchain       = swapchain_wrapper;

    swapchain_get_images(arena, ctx, render_target);
}

void cleanup_swapchain(rop(ro GraphicsContext) ctx, rop(rw Swapchain) swapchain) {
    for (u32 idx = 0; idx < swapchain->image_count; idx++) {
        cleanup_image_view(ctx, swapchain->images + idx);
        cleanup_sync_objects(ctx, swapchain->sync_objects + idx);
    }
    // TODO these were allocated by the arena... damn
    // swapchain->images = NULL;
    // swapchain->sync_objects = NULL;

    vkDestroySwapchainKHR(ctx->device, swapchain->handle, NULL);
}
