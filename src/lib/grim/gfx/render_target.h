#pragma once

#if !defined(WAYLAND) && !defined(X11)
#define WAYLAND
#endif
#if defined(WAYLAND)
#include <lib/grim/os/window_wayland.h>
#include <vulkan/vulkan_wayland.h>
#define GRIM_SURFACE_EXTENSION VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME
#else
#include <lib/grim/os/window_x11.h>
#include <vulkan/vulkan_xlib.h>
#define GRIM_SURFACE_EXTENSION VK_KHR_XLIB_SURFACE_EXTENSION_NAME
#endif

RenderTarget window_create(Arena *arena, u32 width, u32 height) {
    GrimWindow *window = arena_alloc_aligned(arena, GrimWindow, 1);
    window_open(window, width, height);
    return (RenderTarget){.window = window, .extent = {width, height}};
}

static VkSurfaceKHR surface_create(VkInstance instance, const GrimWindow *window) {
    VkSurfaceKHR surface;
#if defined(WAYLAND)
    VkWaylandSurfaceCreateInfoKHR info = {.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .display = window->display, .surface = window->surface};
    check_vkresult(vkCreateWaylandSurfaceKHR(instance, &info, NULL, &surface), SCOPE_GFX_INIT, "Create Wayland Vulkan surface");
#else
    VkXlibSurfaceCreateInfoKHR info = {.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = window->display, .window = window->x11_window};
    check_vkresult(vkCreateXlibSurfaceKHR(instance, &info, NULL, &surface), SCOPE_GFX_INIT, "Create X11 Vulkan surface");
#endif
    return surface;
}
