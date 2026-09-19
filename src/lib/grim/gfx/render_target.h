#pragma once

#include <lib/grim/gfx/internal_graphics.h>

#define X11
#if defined(WAYLAND)
#include <lib/grim/os/window_wayland.h>
#include <vulkan/vulkan_wayland.h>
#elif defined(X11)
#include <lib/grim/os/window_x11.h>
#include <vulkan/vulkan_xlib.h>
#endif

RenderTarget window_create(rop(rw Arena) arena, ro u32 width, ro u32 height) {
    GrimWindow* window = arena_alloc_align(arena, sizeof(void*), sizeof(GrimWindow));
    *window            = window_open(width, height);

    // window_lock_pointer();
    window_setup_listeners(window);

    RenderTarget render_target  = {0};
    render_target.window        = window;
    render_target.extent.width  = width;
    render_target.extent.height = height;

    return render_target;
}

#if defined(WAYLAND)
VkSurfaceKHR surface_create(ro VkInstance instance, rop(ro GrimWindow) window) {
    VkWaylandSurfaceCreateInfoKHR create_info = {0};
    create_info.sType                         = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    create_info.display                       = window->display;
    create_info.surface                       = window->surface;
    create_info.pNext                         = NULL;

    VkSurfaceKHR surface;
    vkCreateWaylandSurfaceKHR(instance, &create_info, NULL, &surface);
    return surface;
}

#elif defined(X11)
VkSurfaceKHR surface_create(ro VkInstance instance, rop(ro GrimWindow) window) {
    VkXlibSurfaceCreateInfoKHR create_info = {0};
    create_info.sType                      = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    create_info.dpy                        = window->display;
    create_info.window                     = window->x11_window;
    create_info.pNext                      = NULL;

    VkSurfaceKHR surface;
    vkCreateXlibSurfaceKHR(instance, &create_info, NULL, &surface);
    return surface;
}
#endif

void render_target_update_extent(rop(rw RenderTarget) render_target) {
    render_target->extent.width  = render_target->window->width;
    render_target->extent.height = render_target->window->height;
    DEBUG_LOG(SCOPE_GFX_SWAPCHAIN, "Resizing to: %u %u", render_target->extent.width, render_target->extent.height);
}

void cleanup_render_target(rop(ro GraphicsContext) ctx, rop(rw RenderTarget) render_target) {
    if (render_target->swapchain.handle != VK_NULL_HANDLE) {
        cleanup_swapchain(ctx, &render_target->swapchain);
    }

    if (render_target->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(ctx->instance, render_target->surface, NULL);
        render_target->surface = VK_NULL_HANDLE;
    }
}
