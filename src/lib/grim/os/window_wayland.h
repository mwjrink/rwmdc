#pragma once

#include <fcntl.h>
#include <lib/grim/bp.h>
#include <lib/grim/logger.h>
#include <lib/grim/mem/arena.h>
#include <string.h>

// #ifdef WAYLAND // GLFW literally checks the env for runtime shit
#include <sys/mman.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

// TODO unify these into a header?
#include <lib/wayland/pointer-constraints.h>
#include <lib/wayland/relative-pointer.h>
#include <lib/wayland/xdg-shell.h>

typedef struct GrimWindow {
    struct wl_display*                      display;
    struct wl_surface*                      surface;
    struct wl_compositor*                   compositor;
    struct wl_pointer*                      pointer;
    struct wl_keyboard*                     keyboard;
    struct wl_seat*                         seat;
    // struct wl_shm*        shm;
    // xdg
    struct wl_shell_surface*                shell_surface;
    struct xdg_wm_base*                     xdg_base;
    // struct wl_xdg_shell* xdg_shell;
    struct xdg_toplevel*                    xdg_toplevel;
    struct xdg_surface*                     xdg_surface;
    // struct wl_output*     output;
    struct zwp_relative_pointer_manager_v1* relative_pointer_manager;
    struct zwp_relative_pointer_v1*         relative_pointer;
    struct zwp_pointer_constraints_v1*      pointer_constraints;
    struct zwp_locked_pointer_v1*           locked_pointer;
    struct wl_region*                       region;

    u32 width;
    u32 height;

    int mouse_dx;
    int mouse_dy;

    u32 request_close;

    int epoll_fd;
    int wl_fd;
} GrimWindow;
STATIC_ASSERT(sizeof(GrimWindow) == 152);

// static const struct wl_registry_listener registry_listener;
static const struct wl_pointer_listener              pointer_listener;
static const struct wl_keyboard_listener             keyboard_listener;
static const struct xdg_surface_listener             xdg_surface_listener;
static const struct xdg_toplevel_listener            xdg_toplevel_listener;
static const struct zwp_locked_pointer_v1_listener   locked_pointer_listener;
static const struct wl_shell_surface_listener        shell_surface_listener;
static const struct xdg_wm_base_listener             xdg_base_listener;
static const struct zwp_relative_pointer_v1_listener relative_pointer_listener;

static void registry_global(void* data, struct wl_registry* registry, u32 name, const char* interface, u32 version) {
    GrimWindow* window = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        VERBOSE_LOG(SCOPE_WAYLAND, "compositor");
        window->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, min(version, 4));
        // } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        //     VERBOSE_LOG(SCOPE_WAYLAND,"shm");
        //     window->shm = wl_registry_bind(registry, name, &wl_shm_interface, min(version, 1));
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        VERBOSE_LOG(SCOPE_WAYLAND, "xdg_wm_base");
        window->xdg_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, min(version, 1));
    } else if (strcmp(interface, wl_shell_surface_interface.name) == 0) {
        VERBOSE_LOG(SCOPE_WAYLAND, "wl_shell_surface");
        window->shell_surface = wl_registry_bind(registry, name, &wl_shell_surface_interface, min(version, 1));
        // window->xdg_shell = wl_registry_bind(registry, name, &xdg_wm_base_interface, min(version, 1));
        // } else if (strcmp(interface, wl_shell_interface.name) == 0) {
        //     VERBOSE_LOG(SCOPE_WAYLAND,"shell_interface");
        //     window->shell = wl_registry_bind(registry, name, &wl_shell_interface, min(version, 1));
        // } else if (strcmp(interface, wl_output_interface.name) == 0) {
        //     VERBOSE_LOG(SCOPE_WAYLAND,"output");
        //     window->output = wl_registry_bind(registry, name, &wl_output_interface, min(version, 1));
        // } else if (strcmp(interface, xdg_activation.name) == 0) {
        //     VERBOSE_LOG(SCOPE_WAYLAND,"xdg activation");
        //     window->output = wl_registry_bind(registry, name, &wl_output_interface, min(version, 1));
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        VERBOSE_LOG(SCOPE_WAYLAND, "seat_interface");
        window->seat = wl_registry_bind(registry, name, &wl_seat_interface, min(version, 2));
    } else if (strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0) {
        VERBOSE_LOG(SCOPE_WAYLAND, "relative_pointer_manager");
        window->relative_pointer_manager =
            wl_registry_bind(registry, name, &zwp_relative_pointer_manager_v1_interface, min(version, 1));
        VERBOSE_LOG(SCOPE_WAYLAND, "WE GOT: %p", window->relative_pointer_manager);
    } else if (strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0) {
        VERBOSE_LOG(SCOPE_WAYLAND, "pointer_constraints");
        window->pointer_constraints =
            wl_registry_bind(registry, name, &zwp_pointer_constraints_v1_interface, min(version, 1));
        // } else if (strcmp(interface, wl_region_interface.name) == 0) {
        //     VERBOSE_LOG(SCOPE_WAYLAND,"region");
        //     window->region = wl_registry_bind(registry, name, &wl_region_interface, min(version, 1));
    } else {
        // VERBOSE_LOG(SCOPE_WAYLAND,"what we got: %s", interface);
    }
}

static void registry_global_remove(void* a, struct wl_registry* b, u32 c) {
}

static void xdg_configure(
    void* data, struct xdg_toplevel* xdg_toplevel, int32_t width, int32_t height, struct wl_array* states) {
    GrimWindow* window = data;

    VERBOSE_LOG(SCOPE_WAYLAND, "Configuring xdg");
    if (width == 0 || height == 0) {
        // TODO get wl_output size
        // 1920, 1080
        xdg_surface_set_window_geometry(window->xdg_surface, 960, 540, 1920, 1080);
        // Not sure if another configure is fired from the set_geom call
        DEBUG_LOG(SCOPE_WAYLAND, "We requested: %u %u", width, height);
        window->width  = 1920;
        window->height = 1080;
    } else {
        DEBUG_LOG(SCOPE_WAYLAND, "Compositor requested: %u %u", width, height);
        window->width  = width;
        window->height = height;
    }

    xdg_surface_ack_configure(window->xdg_surface, 0);

    wl_surface_commit(window->surface);
}
static void xdg_close(void* data, struct xdg_toplevel* xdg_toplevel) {
}
static void xdg_configure_bounds(void* data, struct xdg_toplevel* xdg_toplevel, int32_t width, int32_t height) {
}
static void xdg_wm_capabilities(void* data, struct xdg_toplevel* xdg_toplevel, struct wl_array* capabilities) {
}

static void xdg_surface_configure(void* data, struct xdg_surface* xdg_surface, uint32_t serial) {
    GrimWindow* window = data;
    xdg_surface_ack_configure(xdg_surface, serial);

    // wl_surface_attach(window->surface, buffer, 0, 0);
    wl_surface_commit(window->surface);

    VERBOSE_LOG(SCOPE_WAYLAND, "Configured xdg surface!");
}

#include <sys/stat.h>
typedef u32 pixel;
struct pool_data {
    int      fd;
    pixel*   memory;
    unsigned capacity;
    unsigned size;
};

GrimWindow window_open(u32 width, u32 height) {
    GrimWindow window = {0};
    window.display    = wl_display_connect(NULL);
    if (window.display == NULL) {
        CRITICAL_LOG(SCOPE_WAYLAND, "Failed to connect to wayland display.");
        exit(1);
    }

    struct wl_registry*         registry = wl_display_get_registry(window.display);
    struct wl_registry_listener listener = {
        .global        = registry_global,
        .global_remove = registry_global_remove,
    };
    wl_registry_add_listener(registry, &listener, &window);
    wl_display_roundtrip(window.display); // get all the stuff through the callback
    wl_registry_destroy(registry);

    // wl_seat_

    window.surface                    = wl_compositor_create_surface(window.compositor);
    struct xdg_surface* xdg_surface   = xdg_wm_base_get_xdg_surface(window.xdg_base, window.surface);
    window.xdg_surface                = xdg_surface;
    struct xdg_toplevel* xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
    window.xdg_toplevel               = xdg_toplevel;

    xdg_toplevel_set_app_id(xdg_toplevel, "dev-float");
    xdg_toplevel_set_title(xdg_toplevel, "ritual");
    // xdg_toplevel_set_min_size(xdg_toplevel, width, height);

    xdg_surface_set_window_geometry(window.xdg_surface, 0, 0, width, height);

    window.width  = width;
    window.height = height;

    // However, if you have a more sophisticated application, you can build your own event loop in any manner you
    // please, and obtain the Wayland display's file descriptor with wl_display_get_fd. Upon POLLIN events, call
    // wl_display_dispatch to process incoming events. To flush outgoing requests, call wl_display_flush.

    // struct wl_callback* cb = wl_surface_frame(window.surface);

    // THIS CALL LOCKS A MUTEX AHHHHH
    // wl_display_prepare_read(display);
    // wl_surface_commit(window.surface);

    return window;
}

void window_set_fullscreen(rop(ro GrimWindow) window) {
    xdg_toplevel_set_fullscreen(window->xdg_toplevel, NULL);
}

void window_unset_fullscreen(rop(ro GrimWindow) window) {
    xdg_toplevel_unset_fullscreen(window->xdg_toplevel);
}

void window_lock_pointer(rop(rw GrimWindow) window) {
    if (window->locked_pointer != NULL) {
        return;
    }
    struct zwp_locked_pointer_v1* locked_pointer =
        zwp_pointer_constraints_v1_lock_pointer(window->pointer_constraints,
                                                window->surface,
                                                window->pointer,
                                                NULL,
                                                ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    window->locked_pointer = locked_pointer;

    // TODO I don't care about these events, do I?
    zwp_locked_pointer_v1_add_listener(locked_pointer, &locked_pointer_listener, window);
}

void window_unlock_pointer(rop(rw GrimWindow) window) {
    if (window->locked_pointer == NULL) {
        return;
    }
    zwp_locked_pointer_v1_destroy(window->locked_pointer);
    window->locked_pointer = NULL;
}

void window_setup_listeners(rop(rw GrimWindow) window) {
    xdg_wm_base_add_listener(window->xdg_base, &xdg_base_listener, window);
    xdg_surface_add_listener(window->xdg_surface, &xdg_surface_listener, window);
    xdg_toplevel_add_listener(window->xdg_toplevel, &xdg_toplevel_listener, window);
    window->pointer = wl_seat_get_pointer(window->seat);
    wl_pointer_add_listener(window->pointer, &pointer_listener, window);

    window->keyboard = wl_seat_get_keyboard(window->seat);
    wl_keyboard_add_listener(window->keyboard, &keyboard_listener, window);

    if (window->shell_surface != NULL) {
        wl_shell_surface_add_listener(window->shell_surface, &shell_surface_listener, window);
    }

    window->relative_pointer =
        zwp_relative_pointer_manager_v1_get_relative_pointer(window->relative_pointer_manager, window->pointer);
    zwp_relative_pointer_v1_add_listener(window->relative_pointer, &relative_pointer_listener, window);

    // window->wl_fd = wl_display_get_fd(window->display);
    // int flags     = fcntl(window->wl_fd, F_GETFL, 0);
    // fcntl(window->wl_fd, F_SETFL, flags | O_NONBLOCK);

    i32 flush_result = wl_display_flush(window->display);
    if (flush_result == -1) {
        CRITICAL_LOG(SCOPE_WAYLAND, "Failed to send events to wayland server.");
        exit(1);
    }
    VERBOSE_LOG(SCOPE_WAYLAND, "Sent %i bytes to the wayland server on flush.", flush_result);

    // struct xdg_toplevel_listener toplevel_listener = {
    //     .configure = xdg_configure,
    //     .close = xdg_close,
    //     .configure_bounds = xdg_configure_bounds,
    //     .wm_capabilities = xdg_wm_capabilities,
    // };
    // xdg_toplevel_add_listener(window->xdg_toplevel, &toplevel_listener, window);

    // int epoll = epoll_create1(0);
    // if (epoll == -1) {
    //     CRITICAL_LOG(SCOPE_WAYLAND, "Failed to create epoll instance.");
    //     exit(1);
    // }
    // struct epoll_event event = (struct epoll_event){
    //     .events = EPOLLIN,
    //     .data =
    //         (epoll_data_t){
    //             .ptr = window,
    //         },
    // };
    //
    // epoll_ctl(epoll, EPOLL_CTL_ADD, window->wl_fd, &event);

    // wl_display_dispatch(window->display);
    wl_display_dispatch_pending(window->display);
    wl_display_flush(window->display);
}

void window_poll_events(rop(rw GrimWindow) window) {
    // reset transient polled event state
    window->mouse_dx = 0;
    window->mouse_dy = 0;

    // u8  buff[1024];
    // u64 read_amount = read(window->wl_fd, buff, 1024);
    // DEBUG_LOG(SCOPE_WAYLAND, "Read: %lu", read_amount);

    // wl_display_dispatch(window->display);
    // wl_display_flush(window->display);

    // DEBUG_LOG(SCOPE_WAYLAND, "WAIT");
    // arena_ckpt(arena);
    // const usize buffer_size = 1024 * 1024;
    // void*       data = arena_alloc(arena, buffer_size);
    // usize       read_bytes = read(window->epoll_fd, data, buffer_size);
    // if (read_bytes < 0) {
    //     CRITICAL_LOG(SCOPE_WAYLAND, "An error ocurred reading the events.");
    //     exit(1);
    // }
    // arena_pop(arena);
    // DEBUG_LOG(SCOPE_WAYLAND, "WAIT");

    // wl_display_dispatch(window->display);

    // DEBUG_LOG(SCOPE_WAYLAND, "WAIT");
    wl_display_roundtrip(window->display); // get all the stuff through the callbacks
    // DEBUG_LOG(SCOPE_WAYLAND, "WAIT");
    // wl_display_dispatch(window->display);
    // NON BLOCKING VERSION
    wl_display_dispatch_pending(window->display);
    // DEBUG_LOG(SCOPE_WAYLAND, "WAIT");
    wl_display_flush(window->display);
    // DEBUG_LOG(SCOPE_WAYLAND, "WAIT");
}

struct pointer_data {
    struct wl_surface* surface;
    struct wl_buffer*  buffer;
    int32_t            hot_spot_x;
    int32_t            hot_spot_y;
    struct wl_surface* target_surface;
};

void close_window(GrimWindow* window) {
    // close(window->epoll_fd);
    // window->epoll_fd = -1;

    if (window->locked_pointer != NULL) {
        zwp_locked_pointer_v1_destroy(window->locked_pointer);
        window->locked_pointer = NULL;
    }

    zwp_pointer_constraints_v1_destroy(window->pointer_constraints);
    window->pointer_constraints = NULL;

    zwp_relative_pointer_v1_destroy(window->relative_pointer);
    window->relative_pointer = NULL;

    zwp_relative_pointer_manager_v1_destroy(window->relative_pointer_manager);
    window->relative_pointer_manager = NULL;

    xdg_toplevel_destroy(window->xdg_toplevel);
    window->xdg_toplevel = NULL;

    xdg_surface_destroy(window->xdg_surface);
    window->xdg_surface = NULL;

    xdg_wm_base_destroy(window->xdg_base);
    window->xdg_base = NULL;

    wl_pointer_destroy(window->pointer);
    window->pointer = NULL;

    wl_keyboard_destroy(window->keyboard);
    window->keyboard = NULL;

    wl_seat_destroy(window->seat);
    window->seat = NULL;

    wl_surface_destroy(window->surface);
    window->surface = NULL;

    wl_compositor_destroy(window->compositor);
    window->compositor = NULL;

    // TODO if we have multiple windows, don't
    // destroy everything until all windows are closed.
    wl_display_disconnect(window->display);
    window->display = NULL;
}

static void pointer_enter(void*              data,
                          struct wl_pointer* wl_pointer,
                          uint32_t           serial,
                          struct wl_surface* surface,
                          wl_fixed_t         surface_x,
                          wl_fixed_t         surface_y) {
    // struct pointer_data* pointer_data;

    VERBOSE_LOG(SCOPE_WAYLAND, "Pointer enter!");

    // pointer_data = wl_pointer_get_user_data(wl_pointer);
    // pointer_data->target_surface = surface;
    // wl_surface_attach(pointer_data->surface, pointer_data->buffer, 0, 0);
    // wl_surface_commit(pointer_data->surface);
    // wl_pointer_set_cursor(
    //     wl_pointer, serial, pointer_data->surface, pointer_data->hot_spot_x, pointer_data->hot_spot_y);
}

static void pointer_leave(void* data, struct wl_pointer* wl_pointer, uint32_t serial, struct wl_surface* wl_surface) {
    VERBOSE_LOG(SCOPE_WAYLAND, "Pointer leave!");
}

static void pointer_motion(
    void* data, struct wl_pointer* wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    // DEBUG_LOG(SCOPE_WAYLAND, "Pointer motion!");
}

static void pointer_button(
    void* data, struct wl_pointer* wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    // struct pointer_data* pointer_data;
    // void (*callback)(uint32_t);

    VERBOSE_LOG(SCOPE_WAYLAND, "Pointer button!");

    // pointer_data = wl_pointer_get_user_data(wl_pointer);
    // callback = wl_surface_get_user_data(pointer_data->target_surface);
    // if (callback != NULL)
    //     callback(button);
}

static void pointer_axis(void* data, struct wl_pointer* wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
    VERBOSE_LOG(SCOPE_WAYLAND, "Pointer axis!");
}

static void locked_pointer_locked(void* data, struct zwp_locked_pointer_v1* zwp_locked_pointer_v1) {
    // GrimWindow* window = data;

    VERBOSE_LOG(SCOPE_WAYLAND, "Locked pointer locked.");
}

static void locked_pointer_unlocked(void* data, struct zwp_locked_pointer_v1* zwp_locked_pointer_v1) {
    // GrimWindow* window = data;

    VERBOSE_LOG(SCOPE_WAYLAND, "Locked pointer unlocked.");
}

static const struct zwp_locked_pointer_v1_listener locked_pointer_listener = {
    .locked   = locked_pointer_locked,
    .unlocked = locked_pointer_unlocked,
};

static void ping(void* data, struct wl_shell_surface* wl_shell_surface, uint32_t serial) {
    // This may not be necessary at all, it is never called on my hyprland but xdg_ping is
    VERBOSE_LOG(SCOPE_WAYLAND, "PONGING");
    wl_shell_surface_pong(wl_shell_surface, serial);
}

static void xdg_ping(void* data, struct xdg_wm_base* xdg_wm_base, uint32_t serial) {
    VERBOSE_LOG(SCOPE_WAYLAND, "XDG PONGING");
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static void keyboard_keymap(void* data, struct wl_keyboard* wl_keyboard, uint32_t format, int32_t fd, uint32_t size) {
    //
    VERBOSE_LOG(SCOPE_WAYLAND, "KEYMAP");
    // void* keymap = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    // DEBUG_LOG(SCOPE_WAYLAND, "%s", (char*)keymap);
    // munmap(keymap, size);
}

static void keyboard_enter(
    void* data, struct wl_keyboard* wl_keyboard, uint32_t serial, struct wl_surface* surface, struct wl_array* keys) {
    //
    VERBOSE_LOG(SCOPE_WAYLAND, "KEY ENTER");
}

static void keyboard_leave(void* data, struct wl_keyboard* wl_keyboard, uint32_t serial, struct wl_surface* surface) {
    //
    VERBOSE_LOG(SCOPE_WAYLAND, "KEY LEAVE");
}

static void keyboard_key(
    void* data, struct wl_keyboard* wl_keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    // GrimWindow* window = data;
    switch (state) {
        case 0: {
            VERBOSE_LOG(SCOPE_WAYLAND, "Key released: %u", key);
        } break;
        case 1: {
            VERBOSE_LOG(SCOPE_WAYLAND, "Key pressed: %u", key);
        } break;
        case 2: {
            VERBOSE_LOG(SCOPE_WAYLAND, "Key repeated: %u", key);
        } break;
        default: {
            CRITICAL_LOG(SCOPE_WAYLAND, "IMPOSSIBLE KEY STATE: %u", state);
        } break;
    }
}

static void keyboard_modifiers(void*               data,
                               struct wl_keyboard* wl_keyboard,
                               uint32_t            serial,
                               uint32_t            mods_depressed,
                               uint32_t            mods_latched,
                               uint32_t            mods_locked,
                               uint32_t            group) {
    //

    VERBOSE_LOG(SCOPE_WAYLAND, "KEY MODS");
}

static void keyboard_repeat_info(void* data, struct wl_keyboard* wl_keyboard, int32_t rate, int32_t delay) {
    //
    VERBOSE_LOG(SCOPE_WAYLAND, "KEY REPEAT INFO");
}

static void relative_pointer_motion(void*                           data,
                                    struct zwp_relative_pointer_v1* zwp_relative_pointer_v1,
                                    uint32_t                        utime_hi,
                                    uint32_t                        utime_lo,
                                    wl_fixed_t                      dx,
                                    wl_fixed_t                      dy,
                                    wl_fixed_t                      dx_unaccel,
                                    wl_fixed_t                      dy_unaccel) {
    // TODO use evdev or /dev/input/by-id/ for input, no need for this
    GrimWindow* window = data;
    // VERBOSE_LOG(SCOPE_WAYLAND, "Mouse moved: x%i y%i", dx_unaccel, dy_unaccel);
    window->mouse_dx   = dx_unaccel;
    window->mouse_dy   = dy_unaccel;
}

void xdg_toplevel_configure(
    void* data, struct xdg_toplevel* xdg_toplevel, int32_t width, int32_t height, struct wl_array* states) {
    GrimWindow* window = data;

    VERBOSE_LOG(SCOPE_WAYLAND, "Configuring toplevel xdg");
    if (width == 0 || height == 0) {
        DEBUG_LOG(SCOPE_WAYLAND, "We requested: %u %u", 1920, 1080);
        window->width  = 1920;
        window->height = 1080;
    } else {
        DEBUG_LOG(SCOPE_WAYLAND, "Compositor requested: %u %u", width, height);
        window->width  = width;
        window->height = height;
    }
    // TODO get wl_output size
    // TODO set the proper location on the screen/calculate it
    // Not sure if another configure is fired from the set_geom call
    xdg_surface_set_window_geometry(window->xdg_surface, 960, 540, window->width, window->height);

    xdg_surface_ack_configure(window->xdg_surface, 0);
}

void xdg_toplevel_close(void* data, struct xdg_toplevel* xdg_toplevel) {
    GrimWindow* window    = data;
    window->request_close = true;
}

void xdg_toplevel_configure_bounds(void* data, struct xdg_toplevel* xdg_toplevel, int32_t width, int32_t height) {
}

void xdg_toplevel_wm_capabilities(void* data, struct xdg_toplevel* xdg_toplevel, struct wl_array* capabilities) {
}

static const struct wl_pointer_listener  pointer_listener     = {.enter  = pointer_enter,
                                                                 .leave  = pointer_leave,
                                                                 .motion = pointer_motion,
                                                                 .button = pointer_button,
                                                                 .axis   = pointer_axis};
static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static const struct wl_shell_surface_listener shell_surface_listener = {
    .ping = ping,
};

static const struct xdg_wm_base_listener xdg_base_listener = {
    .ping = xdg_ping,
};

static const struct wl_keyboard_listener keyboard_listener = {
    /**
     * keyboard mapping
     *
     * this event provides a file descriptor to the client which can
     * be memory-mapped in read-only mode to provide a keyboard mapping
     * description.
     *
     * from version 7 onwards, the fd must be mapped with map_private
     * by the recipient, as map_shared may fail.
     * @param format keymap format
     * @param fd keymap file descriptor
     * @param size keymap size, in bytes
     */
    // void(*keymap)(void* data, struct wl_keyboard* wl_keyboard, uint32_t format, int32_t fd, uint32_t size);
    .keymap      = keyboard_keymap,
    /**
     * enter event
     *
     * notification that this seat's keyboard focus is on a certain
     * surface.
     *
     * the compositor must send the wl_keyboard.modifiers event after
     * this event.
     *
     * in the wl_keyboard logical state, this event sets the active
     * surface to the surface argument and the keys currently logically
     * down to the keys in the keys argument. the compositor must not
     * send this event if the wl_keyboard already had an active surface
     * immediately before this event.
     *
     * clients should not use the list of pressed keys to emulate
     * key-press events. the order of keys in the list is unspecified.
     * @param serial serial number of the enter event
     * @param surface surface gaining keyboard focus
     * @param keys the keys currently logically down
     */
    // void (*enter)(void*               data,
    //               struct wl_keyboard* wl_keyboard,
    //               uint32_t            serial,
    //               struct wl_surface*  surface,
    //               struct wl_array*    keys);
    .enter       = keyboard_enter,
    /**
     * leave event
     *
     * notification that this seat's keyboard focus is no longer on a
     * certain surface.
     *
     * the leave notification is sent before the enter notification for
     * the new focus.
     *
     * in the wl_keyboard logical state, this event resets all values
     * to their defaults. the compositor must not send this event if
     * the active surface of the wl_keyboard was not equal to the
     * surface argument immediately before this event.
     * @param serial serial number of the leave event
     * @param surface surface that lost keyboard focus
     */
    // void (*leave)(void* data, struct wl_keyboard* wl_keyboard, uint32_t serial, struct wl_surface* surface);
    .leave       = keyboard_leave,
    /**
     * key event
     *
     * a key was pressed or released. the time argument is a
     * timestamp with millisecond granularity, with an undefined base.
     *
     * the key is a platform-specific key code that can be interpreted
     * by feeding it to the keyboard mapping (see the keymap event).
     *
     * if this event produces a change in modifiers, then the resulting
     * wl_keyboard.modifiers event must be sent after this event.
     *
     * in the wl_keyboard logical state, this event adds the key to the
     * keys currently logically down (if the state argument is pressed)
     * or removes the key from the keys currently logically down (if
     * the state argument is released). the compositor must not send
     * this event if the wl_keyboard did not have an active surface
     * immediately before this event. the compositor must not send this
     * event if state is pressed (resp. released) and the key was
     * already logically down (resp. was not logically down)
     * immediately before this event.
     *
     * since version 10, compositors may send key events with the
     * "repeated" key state when a wl_keyboard.repeat_info event with a
     * rate argument of 0 has been received. this allows the compositor
     * to take over the responsibility of key repetition.
     * @param serial serial number of the key event
     * @param time timestamp with millisecond granularity
     * @param key key that produced the event
     * @param state physical state of the key
     */
    // void (*key)(
    //     void* data, struct wl_keyboard* wl_keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
    .key         = keyboard_key,
    /**
     * modifier and group state
     *
     * notifies clients that the modifier and/or group state has
     * changed, and it should update its local state.
     *
     * the compositor may send this event without a surface of the
     * client having keyboard focus, for example to tie modifier
     * information to pointer focus instead. if a modifier event with
     * pressed modifiers is sent without a prior enter event, the
     * client can assume the modifier state is valid until it receives
     * the next wl_keyboard.modifiers event. in order to reset the
     * modifier state again, the compositor can send a
     * wl_keyboard.modifiers event with no pressed modifiers.
     *
     * in the wl_keyboard logical state, this event updates the
     * modifiers and group.
     * @param serial serial number of the modifiers event
     * @param mods_depressed depressed modifiers
     * @param mods_latched latched modifiers
     * @param mods_locked locked modifiers
     * @param group keyboard layout
     */
    // void (*modifiers)(void*               data,
    //                   struct wl_keyboard* wl_keyboard,
    //                   uint32_t            serial,
    //                   uint32_t            mods_depressed,
    //                   uint32_t            mods_latched,
    //                   uint32_t            mods_locked,
    //                   uint32_t            group);
    .modifiers   = keyboard_modifiers,
    /**
     * repeat rate and delay
     *
     * informs the client about the keyboard's repeat rate and delay.
     *
     * this event is sent as soon as the wl_keyboard object has been
     * created, and is guaranteed to be received by the client before
     * any key press event.
     *
     * negative values for either rate or delay are illegal. a rate of
     * zero will disable any repeating (regardless of the value of
     * delay).
     *
     * this event can be sent later on as well with a new value if
     * necessary, so clients should continue listening for the event
     * past the creation of wl_keyboard.
     * @param rate the rate of repeating keys in characters per second
     * @param delay delay in milliseconds since key down until repeating starts
     * @since 4
     */
    // void (*repeat_info)(void* data, struct wl_keyboard* wl_keyboard, int32_t rate, int32_t delay);
    .repeat_info = keyboard_repeat_info,
};

static const struct zwp_relative_pointer_v1_listener relative_pointer_listener = {
    /**
     * relative pointer motion
     *
     * relative x/y pointer motion from the pointer of the seat
     * associated with this object.
     *
     * a relative motion is in the same dimension as regular wl_pointer
     * motion events, except they do not represent an absolute
     * position. for example, moving a pointer from (x, y) to (x', y')
     * would have the equivalent relative motion (x' - x, y' - y). if a
     * pointer motion caused the absolute pointer position to be
     * clipped by for example the edge of the monitor, the relative
     * motion is unaffected by the clipping and will represent the
     * unclipped motion.
     *
     * this event also contains non-accelerated motion deltas. the
     * non-accelerated delta is, when applicable, the regular pointer
     * motion delta as it was before having applied motion acceleration
     * and other transformations such as normalization.
     *
     * note that the non-accelerated delta does not represent 'raw'
     * events as they were read from some device. pointer motion
     * acceleration is device- and configuration-specific and
     * non-accelerated deltas and accelerated deltas may have the same
     * value on some devices.
     *
     * relative motions are not coupled to wl_pointer.motion events,
     * and can be sent in combination with such events, but also
     * independently. there may also be scenarios where
     * wl_pointer.motion is sent, but there is no relative motion. the
     * order of an absolute and relative motion event originating from
     * the same physical motion is not guaranteed.
     *
     * if the client needs button events or focus state, it can receive
     * them from a wl_pointer object of the same seat that the
     * wp_relative_pointer object is associated with.
     * @param utime_hi high 32 bits of a 64 bit timestamp with microsecond granularity
     * @param utime_lo low 32 bits of a 64 bit timestamp with microsecond granularity
     * @param dx the x component of the motion vector
     * @param dy the y component of the motion vector
     * @param dx_unaccel the x component of the unaccelerated motion vector
     * @param dy_unaccel the y component of the unaccelerated motion vector
     */
    // void (*relative_motion)(void*                           data,
    //                         struct zwp_relative_pointer_v1* zwp_relative_pointer_v1,
    //                         uint32_t                        utime_hi,
    //                         uint32_t                        utime_lo,
    //                         wl_fixed_t                      dx,
    //                         wl_fixed_t                      dy,
    //                         wl_fixed_t                      dx_unaccel,
    //                         wl_fixed_t                      dy_unaccel);
    .relative_motion = relative_pointer_motion,
};

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    /**
     * suggest a surface change
     *
     * this configure event asks the client to resize its toplevel
     * surface or to change its state. the configured state should not
     * be applied immediately. see xdg_surface.configure for details.
     *
     * the width and height arguments specify a hint to the window
     * about how its surface should be resized in window geometry
     * coordinates. see set_window_geometry.
     *
     * if the width or height arguments are zero, it means the client
     * should decide its own window dimension. this may happen when the
     * compositor needs to configure the state of the surface but
     * doesn't have any information about any previous or expected
     * dimension.
     *
     * the states listed in the event specify how the width/height
     * arguments should be interpreted, and possibly how it should be
     * drawn.
     *
     * clients must send an ack_configure in response to this event.
     * see xdg_surface.configure and xdg_surface.ack_configure for
     * details.
     */
    // void (*configure)(void *data,
    // 		  struct xdg_toplevel *xdg_toplevel,
    // 		  int32_t width,
    // 		  int32_t height,
    // 		  struct wl_array *states);
    .configure        = xdg_toplevel_configure,
    /**
     * surface wants to be closed
     *
     * the close event is sent by the compositor when the user wants
     * the surface to be closed. this should be equivalent to the user
     * clicking the close button in client-side decorations, if your
     * application has any.
     *
     * this is only a request that the user intends to close the
     * window. the client may choose to ignore this request, or show a
     * dialog to ask the user to save their data, etc.
     */
    // void (*close)(void *data,
    // 	      struct xdg_toplevel *xdg_toplevel);
    .close            = xdg_toplevel_close,
    /**
     * recommended window geometry bounds
     *
     * the configure_bounds event may be sent prior to a
     * xdg_toplevel.configure event to communicate the bounds a window
     * geometry size is recommended to constrain to.
     *
     * the passed width and height are in surface coordinate space. if
     * width and height are 0, it means bounds is unknown and
     * equivalent to as if no configure_bounds event was ever sent for
     * this surface.
     *
     * the bounds can for example correspond to the size of a monitor
     * excluding any panels or other shell components, so that a
     * surface isn't created in a way that it cannot fit.
     *
     * the bounds may change at any point, and in such a case, a new
     * xdg_toplevel.configure_bounds will be sent, followed by
     * xdg_toplevel.configure and xdg_surface.configure.
     * @since 4
     */
    // void (*configure_bounds)(void* data, struct xdg_toplevel* xdg_toplevel, int32_t width, int32_t height);
    .configure_bounds = xdg_toplevel_configure_bounds,
    /**
     * compositor capabilities
     *
     * this event advertises the capabilities supported by the
     * compositor. if a capability isn't supported, clients should hide
     * or disable the ui elements that expose this functionality. for
     * instance, if the compositor doesn't advertise support for
     * minimized toplevels, a button triggering the set_minimized
     * request should not be displayed.
     *
     * the compositor will ignore requests it doesn't support. for
     * instance, a compositor which doesn't advertise support for
     * minimized will ignore set_minimized requests.
     *
     * compositors must send this event once before the first
     * xdg_surface.configure event. when the capabilities change,
     * compositors must send this event again and then send an
     * xdg_surface.configure event.
     *
     * the configured state should not be applied immediately. see
     * xdg_surface.configure for details.
     *
     * the capabilities are sent as an array of 32-bit unsigned
     * integers in native endianness.
     * @param capabilities array of 32-bit capabilities
     * @since 5
     */
    // void (*wm_capabilities)(void* data, struct xdg_toplevel* xdg_toplevel, struct wl_array* capabilities);
    .wm_capabilities  = xdg_toplevel_wm_capabilities,
};

// #endif

#include <lib/wayland/pointer-constraints.c>
#include <lib/wayland/relative-pointer.c>
#include <lib/wayland/xdg-shell.c>
