#pragma once


VkImageView create_image_view(const GraphicsContext *ctx, VkImage image, VkFormat format) {
    VkImageView view;
    VkImageViewCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = format,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}};
    check_vkresult(vkCreateImageView(ctx->device, &info, NULL, &view), SCOPE_GFX_IMAGE, "Create image view");
    return view;
}

