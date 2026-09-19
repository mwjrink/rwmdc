#pragma once

#include <lib/grim/bp.h>
#include <lib/grim/logger.h>

#include <X11/Xlib.h>

typedef struct GrimWindow {
    Display* display;
    Window   x11_window;
    Screen*  screen;
    // int       screen_id;
    Atom     atom_wm_delete_window;

    i32 x11_fd;

    u32 width;
    u32 height;

    int mouse_dx;
    int mouse_dy;

    u32 request_close;

    struct {
        u32 pointer_locked : 1;
        u32 _padding       : 31;
    };
    u32 __padding;

    // int epoll_fd;
    // int wl_fd;
} GrimWindow;
STATIC_ASSERT(sizeof(GrimWindow) == 64);

GrimWindow window_open(u32 width, u32 height) {
    Display* display;
    Window   x11_window;
    Screen*  screen;
    int      screen_id;

    display = XOpenDisplay(NULL);
    if (display == NULL) {
        CRITICAL_LOG(SCOPE_X11, "%s\n", "Failed to open display");
        exit(1);
    }
    screen    = DefaultScreenOfDisplay(display);
    screen_id = DefaultScreen(display);

    GrimWindow window = {0};

    x11_window = XCreateSimpleWindow(display,
                                     RootWindowOfScreen(screen),
                                     0,
                                     0,
                                     width,
                                     height,
                                     1,
                                     BlackPixel(display, screen_id),
                                     WhitePixel(display, screen_id));

    Atom atom_wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, x11_window, &atom_wm_delete_window, 1);

    XSelectInput(display, x11_window, ExposureMask | StructureNotifyMask);

    XStoreName(display, x11_window, "ritual");

    Atom atom_wm_class  = XInternAtom(display, "WM_CLASS", False);
    Atom atom_wm_string = XInternAtom(display, "STRING", False);
    VERBOSE_LOG(SCOPE_X11, "Class: %lu, String: %lu", atom_wm_class, atom_wm_string);
    const char* class  = "dev-float";
    i32         result = XChangeProperty(
        display, x11_window, atom_wm_class, atom_wm_string, 8, PropModeReplace, (u8*)class, (i32)strlen(class) + 1);
    VERBOSE_LOG(SCOPE_X11, "result: %i", result);

    // Show the window
    XClearWindow(display, x11_window);
    XMapRaised(display, x11_window);
    XFlush(display);

    i32 x11_fd = ConnectionNumber(display);

    // xdg_toplevel_set_app_id(xdg_toplevel, "dev-float");
    // xdg_toplevel_set_title(xdg_toplevel, "ritual");
    // xdg_toplevel_set_min_size(xdg_toplevel, width, height);

    // xdg_surface_set_window_geometry(window.xdg_surface, 0, 0, width, height);

    // window.width  = width;
    // window.height = height;

    window.display               = display;
    window.x11_window            = x11_window;
    window.screen                = screen;
    // window.screen_id             = screen_id;
    window.atom_wm_delete_window = atom_wm_delete_window;

    window.x11_fd = x11_fd;

    window.width  = width;
    window.height = height;

    window.mouse_dx = 0;
    window.mouse_dy = 0;

    window.request_close = false;

    // window.epoll_fd = ;
    // window.wl_fd = ;

    return window;
}

void window_set_fullscreen(rop(ro GrimWindow) window) {
}

void window_unset_fullscreen(rop(ro GrimWindow) window) {
}

void window_lock_pointer(rop(rw GrimWindow) window) {
    window->pointer_locked = true;
}

void window_unlock_pointer(rop(rw GrimWindow) window) {
    window->pointer_locked = false;
}

void window_setup_listeners(rop(rw GrimWindow) window) {
}

void window_poll_events(rop(rw GrimWindow) window) {
    XEvent ev;
    // Handle XEvents and flush the input
    while (XPending(window->display)) {
        XNextEvent(window->display, &ev);

        if (ev.type == ClientMessage) {
            if (ev.xclient.data.l[0] == (long)window->atom_wm_delete_window) {
                window->request_close = true;
                break;
            }
        }

        switch (ev.type) {
            case DestroyNotify:
                window->request_close = true;
                break;
            case Expose: {
                XWindowAttributes attribs = {0};
                XGetWindowAttributes(window->display, window->x11_window, &attribs);
                window->width  = attribs.width;
                window->height = attribs.height;
            } break;
        }
    }

    if (window->pointer_locked) {
        XWarpPointer(window->display, window->x11_window, window->x11_window, 0, 0, 0, 0, 0, 0);
    }
}

void close_window(GrimWindow* window) {
    XDestroyWindow(window->display, window->x11_window);
    XFree(window->screen);
    XCloseDisplay(window->display);
}
