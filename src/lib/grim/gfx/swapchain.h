#pragma once

static void cleanup_swapchain(const GraphicsContext *ctx, Swapchain *swapchain) {
    for (u32 i = 0; i < swapchain->image_count; i++) {
        vkDestroyImageView(ctx->device, swapchain->images[i].view, NULL);
        vkDestroySemaphore(ctx->device, swapchain->render_finished[i], NULL);
    }
    if (swapchain->handle) vkDestroySwapchainKHR(ctx->device, swapchain->handle, NULL);
    free(swapchain->images);
    free(swapchain->render_finished);
    *swapchain = (Swapchain){0};
}

static VkSurfaceFormatKHR select_surface_format(const GraphicsContext *ctx, const RenderTarget *target) {
    u32 count = 0;
    check_vkresult(vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, target->surface, &count, NULL), SCOPE_GFX_SWAPCHAIN, "Count surface formats");
    VkSurfaceFormatKHR *formats = graphics_alloc(count, sizeof(*formats));
    check_vkresult(vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->physical_device, target->surface, &count, formats), SCOPE_GFX_SWAPCHAIN, "Read surface formats");
    if (!count) {
        fprintf(stderr, "Surface has no supported formats\n");
        exit(EXIT_FAILURE);
    }
    VkSurfaceFormatKHR selected = formats[0];
    if (count == 1 && selected.format == VK_FORMAT_UNDEFINED) {
        selected.format = target->format ? target->format : VK_FORMAT_B8G8R8A8_SRGB;
    } else if (target->format) {
        // A dynamic-rendering pipeline's attachment format must remain compatible.
        bool found = false;
        for (u32 i = 0; i < count; i++)
            if (formats[i].format == target->format && formats[i].colorSpace == target->color_space) {
                selected = formats[i];
                found = true;
                break;
            }
        if (!found) {
            fprintf(stderr, "Surface format changed; restart rwmd for the new display format\n");
            exit(EXIT_FAILURE);
        }
    } else {
        const VkFormat preferred[] = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB};
        bool found = false;
        for (u32 p = 0; p < 2 && !found; p++)
            for (u32 i = 0; i < count; i++)
                if (formats[i].format == preferred[p] && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                    selected = formats[i];
                    found = true;
                    break;
                }
    }
    free(formats);
    return selected;
}

static VkPresentModeKHR select_present_mode(const GraphicsContext *ctx, VkSurfaceKHR surface) {
    u32 count = 0;
    check_vkresult(vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, surface, &count, NULL), SCOPE_GFX_SWAPCHAIN, "Count present modes");
    VkPresentModeKHR *modes = graphics_alloc(count, sizeof(*modes));
    check_vkresult(vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->physical_device, surface, &count, modes), SCOPE_GFX_SWAPCHAIN, "Read present modes");
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    for (u32 i = 0; i < count; i++)
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) mode = VK_PRESENT_MODE_MAILBOX_KHR;
    for (u32 i = 0; i < count; i++)
        if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    free(modes);
    return mode;
}

static u32 surface_dimension(u32 requested, u32 minimum, u32 maximum) {
    return requested < minimum ? minimum : requested > maximum ? maximum : requested;
}

static bool create_swapchain(const GraphicsContext *ctx, RenderTarget *target) {
    VkSurfaceCapabilitiesKHR caps;
    check_vkresult(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physical_device, target->surface, &caps), SCOPE_GFX_SWAPCHAIN, "Query surface capabilities");
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = surface_dimension(target->window->width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = surface_dimension(target->window->height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (!extent.width || !extent.height || !target->window->width || !target->window->height) return false;
    VkSurfaceFormatKHR format = select_surface_format(ctx, target);
    VkPresentModeKHR mode = select_present_mode(ctx, target->surface);
    u32 image_count = caps.minImageCount < 3 ? 3 : caps.minImageCount;
    if (caps.maxImageCount && image_count > caps.maxImageCount) image_count = caps.maxImageCount;
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    const VkCompositeAlphaFlagBitsKHR alpha_modes[] = {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
    for (u32 i = 0; i < 4; i++)
        if (caps.supportedCompositeAlpha & alpha_modes[i]) { alpha = alpha_modes[i]; break; }
    u32 families[] = {ctx->graphics_family, ctx->present_family};
    bool separate = ctx->graphics_family != ctx->present_family;
    VkSwapchainCreateInfoKHR info = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = target->surface, .minImageCount = image_count, .imageFormat = format.format,
        .imageColorSpace = format.colorSpace, .imageExtent = extent, .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = separate ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = separate ? 2u : 0u, .pQueueFamilyIndices = separate ? families : NULL,
        .preTransform = caps.currentTransform, .compositeAlpha = alpha, .presentMode = mode,
        .clipped = VK_TRUE, .oldSwapchain = target->swapchain.handle};
    Swapchain next = {.present_mode = mode};
    check_vkresult(vkCreateSwapchainKHR(ctx->device, &info, NULL, &next.handle), SCOPE_GFX_SWAPCHAIN, "Create swapchain");
    check_vkresult(vkGetSwapchainImagesKHR(ctx->device, next.handle, &next.image_count, NULL), SCOPE_GFX_SWAPCHAIN, "Count swapchain images");
    VkImage *handles = graphics_alloc(next.image_count, sizeof(*handles));
    check_vkresult(vkGetSwapchainImagesKHR(ctx->device, next.handle, &next.image_count, handles), SCOPE_GFX_SWAPCHAIN, "Read swapchain images");
    next.images = graphics_alloc(next.image_count, sizeof(*next.images));
    next.render_finished = graphics_alloc(next.image_count, sizeof(*next.render_finished));
    VkSemaphoreCreateInfo semaphore_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (u32 i = 0; i < next.image_count; i++) {
        next.images[i] = (Image){.handle = handles[i]};
        next.images[i].view = create_image_view(ctx, handles[i], format.format);
        check_vkresult(vkCreateSemaphore(ctx->device, &semaphore_info, NULL, &next.render_finished[i]), SCOPE_GFX_SWAPCHAIN, "Create image presentation semaphore");
    }
    free(handles);
    cleanup_swapchain(ctx, &target->swapchain);
    target->swapchain = next;
    target->format = format.format;
    target->color_space = format.colorSpace;
    target->extent = extent;
    return true;
}

void cleanup_render_target(const GraphicsContext *ctx, RenderTarget *target) {
    check_vkresult(vkDeviceWaitIdle(ctx->device), SCOPE_GFX_SWAPCHAIN, "Wait before surface cleanup");
    cleanup_swapchain(ctx, &target->swapchain);
    if (target->surface) vkDestroySurfaceKHR(ctx->instance, target->surface, NULL);
    target->surface = VK_NULL_HANDLE;
}
