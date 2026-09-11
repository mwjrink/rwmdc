#pragma once

#include <lib/grim/os/input.h>
#include <fcntl.h>
#include <locale.h>
#include <sys/mman.h>
#include <wayland-client.h>
#include <lib/wayland/xdg-shell.h>

typedef struct WaylandOffer {
    struct WaylandOffer *next;
    struct wl_data_offer *proxy;
    const char *mime;
} WaylandOffer;
typedef struct WaylandWrite {
    struct WaylandWrite *next;
    WindowClipboard *copy;
    int fd;
    u32 offset;
    u64 deadline;
} WaylandWrite;
typedef struct GrimWindow {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_surface *surface;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct wl_pointer *pointer;
    struct xdg_wm_base *xdg_base;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct wl_data_device_manager *data_manager;
    struct wl_data_device *data_device;
    struct wl_data_source *data_source;
    WaylandOffer *offers, *selection, *drag_offer;
    WaylandWrite *writes;
    WindowClipboard *clipboard;
    bool clipboard_pending;
    int paste_fd;
    bool paste_pending;
    u8 *paste_bytes;
    u32 paste_len, paste_cap;
    u64 paste_deadline;
    struct pollfd *wait_fds;
    u32 wait_cap;
    struct xkb_context *xkb_context;
    struct xkb_keymap *keymap;
    struct xkb_state *xkb_state;
    struct xkb_compose_state *compose;
    u32 modifiers, serial, repeat_key;
    i32 repeat_rate, repeat_delay;
    u64 repeat_next;
    u8 *repeat_text;
    u32 repeat_len, repeat_cap;
    bool repeat_composed;
    f32 pointer_x, pointer_y;
    u32 compositor_name, shell_name, seat_name, manager_name;
    u32 width, height;
    u32 pending_width, pending_height;
    bool request_close, configured, focused;
    WindowEvents events;
} GrimWindow;

void window_clear_events(GrimWindow *window) {
    window->events.count = 0;
    window->events.text_len = 0;
}
const u8 *window_event_text(const GrimWindow *window, const WindowEvent *event) {
    if (event->text_offset > window->events.text_len ||
        event->text_len > window->events.text_len - event->text_offset) return NULL;
    return window->events.text ? window->events.text + event->text_offset : NULL;
}
static void wayland_offer_destroy(GrimWindow *window, WaylandOffer *offer) {
    if (!offer) return;
    WaylandOffer **link = &window->offers;
    while (*link && *link != offer) link = &(*link)->next;
    if (*link) *link = offer->next;
    if (window->selection == offer) window->selection = NULL;
    if (window->drag_offer == offer) window->drag_offer = NULL;
    wl_data_offer_destroy(offer->proxy);
    free(offer);
}
static WaylandOffer *wayland_offer_find(GrimWindow *window, struct wl_data_offer *proxy) {
    for (WaylandOffer *o = window->offers; o; o = o->next) if (o->proxy == proxy) return o;
    return NULL;
}
static void wayland_offer_mime(void *data, struct wl_data_offer *proxy, const char *mime) {
    (void)proxy;
    WaylandOffer *offer = data;
    if (!strcmp(mime, "text/plain;charset=utf-8")) offer->mime = "text/plain;charset=utf-8";
    else if (!offer->mime && !strcmp(mime, "text/plain")) offer->mime = "text/plain";
    else if ((!offer->mime || !strcmp(offer->mime, "text/plain")) && !strcmp(mime, "UTF8_STRING"))
        offer->mime = "UTF8_STRING";
}
static void wayland_offer_actions(void *data, struct wl_data_offer *offer, u32 actions) {
    (void)data; (void)offer; (void)actions;
}
static const struct wl_data_offer_listener wayland_offer_listener = {
    .offer = wayland_offer_mime, .source_actions = wayland_offer_actions, .action = wayland_offer_actions,
};
static void wayland_device_offer(void *data, struct wl_data_device *device, struct wl_data_offer *proxy) {
    (void)device;
    GrimWindow *window = data;
    WaylandOffer *offer = window_input_alloc(sizeof(*offer));
    *offer = (WaylandOffer){.next = window->offers, .proxy = proxy};
    window->offers = offer;
    wl_data_offer_add_listener(proxy, &wayland_offer_listener, offer);
}
static void wayland_device_enter(void *data, struct wl_data_device *device, u32 serial,
    struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *proxy) {
    (void)device; (void)surface; (void)x; (void)y;
    GrimWindow *window = data;
    window->drag_offer = wayland_offer_find(window, proxy);
    /* Drag-and-drop is not editor clipboard input; explicitly reject the drop. */
    if (proxy) wl_data_offer_accept(proxy, serial, NULL);
}
static void wayland_device_leave(void *data, struct wl_data_device *device) {
    (void)device;
    GrimWindow *window = data;
    wayland_offer_destroy(window, window->drag_offer);
}
static void wayland_device_motion(void *data, struct wl_data_device *device, u32 time, wl_fixed_t x, wl_fixed_t y) {
    (void)data; (void)device; (void)time; (void)x; (void)y;
}
static void wayland_device_drop(void *data, struct wl_data_device *device) {
    wayland_device_leave(data, device);
}
static void wayland_device_selection(void *data, struct wl_data_device *device, struct wl_data_offer *proxy) {
    (void)device;
    GrimWindow *window = data;
    WaylandOffer *next = wayland_offer_find(window, proxy);
    if (window->selection != next) wayland_offer_destroy(window, window->selection);
    window->selection = next;
}
static const struct wl_data_device_listener wayland_device_listener = {
    .data_offer = wayland_device_offer, .enter = wayland_device_enter, .leave = wayland_device_leave,
    .motion = wayland_device_motion, .drop = wayland_device_drop, .selection = wayland_device_selection,
};
static void wayland_source_target(void *data, struct wl_data_source *source, const char *mime) {
    (void)data; (void)source; (void)mime;
}
static void wayland_source_send(void *data, struct wl_data_source *source, const char *mime, i32 fd) {
    GrimWindow *window = data;
    if (source != window->data_source || !window->clipboard ||
        (strcmp(mime, "text/plain;charset=utf-8") && strcmp(mime, "text/plain") && strcmp(mime, "UTF8_STRING"))) {
        close(fd); return;
    }
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) { close(fd); return; }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    WaylandWrite *transfer = window_input_alloc(sizeof(*transfer));
    *transfer = (WaylandWrite){.next = window->writes, .copy = window->clipboard, .fd = fd,
        .deadline = window_input_now() + 30000000000ull};
    ++transfer->copy->refs;
    window->writes = transfer;
}
static void wayland_source_cancelled(void *data, struct wl_data_source *source) {
    GrimWindow *window = data;
    if (window->data_source == source) window->data_source = NULL;
    wl_data_source_destroy(source);
}
static void wayland_source_drop(void *data, struct wl_data_source *source) { (void)data; (void)source; }
static void wayland_source_action(void *data, struct wl_data_source *source, u32 action) {
    (void)data; (void)source; (void)action;
}
static const struct wl_data_source_listener wayland_source_listener = {
    .target = wayland_source_target, .send = wayland_source_send, .cancelled = wayland_source_cancelled,
    .dnd_drop_performed = wayland_source_drop, .dnd_finished = wayland_source_drop, .action = wayland_source_action,
};
static void wayland_publish_clipboard(GrimWindow *window) {
    if (!window->clipboard_pending || !window->data_device || !window->serial) return;
    if (window->data_source) wl_data_source_destroy(window->data_source);
    window->data_source = wl_data_device_manager_create_data_source(window->data_manager);
    wl_data_source_add_listener(window->data_source, &wayland_source_listener, window);
    wl_data_source_offer(window->data_source, "text/plain;charset=utf-8");
    wl_data_source_offer(window->data_source, "text/plain");
    wl_data_source_offer(window->data_source, "UTF8_STRING");
    wl_data_device_set_selection(window->data_device, window->data_source, window->serial);
    window->clipboard_pending = false;
}
static void wayland_device_create(GrimWindow *window) {
    if (window->data_manager && window->seat && !window->data_device) {
        window->data_device = wl_data_device_manager_get_data_device(window->data_manager, window->seat);
        wl_data_device_add_listener(window->data_device, &wayland_device_listener, window);
        wayland_publish_clipboard(window);
    }
}
void window_clipboard_set(GrimWindow *window, const u8 *bytes, u32 len) {
    WindowClipboard *copy = window_clipboard_new(bytes, len);
    window_clipboard_unref(window->clipboard);
    window->clipboard = copy;
    window->clipboard_pending = true;
    wayland_publish_clipboard(window);
}
static void wayland_paste_complete(GrimWindow *window, const u8 *bytes, u32 len) {
    if (window->paste_pending) window_input_paste(&window->events, bytes, len);
    window->paste_pending = false;
    if (window->paste_fd >= 0) close(window->paste_fd);
    window->paste_fd = -1;
    window->paste_len = 0;
}
void window_clipboard_request(GrimWindow *window) {
    wayland_paste_complete(window, NULL, 0);
    window->paste_pending = true;
    if (window->data_source && window->clipboard) {
        wayland_paste_complete(window, window->clipboard->bytes, window->clipboard->len);
        return;
    }
    if (!window->selection || !window->selection->mime) { wayland_paste_complete(window, NULL, 0); return; }
    int pipes[2];
    if (pipe(pipes) < 0) { wayland_paste_complete(window, NULL, 0); return; }
    fcntl(pipes[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipes[1], F_SETFD, FD_CLOEXEC);
    int flags = fcntl(pipes[0], F_GETFL);
    if (flags < 0 || fcntl(pipes[0], F_SETFL, flags | O_NONBLOCK) < 0) {
        close(pipes[0]); close(pipes[1]); wayland_paste_complete(window, NULL, 0); return;
    }
    wl_data_offer_receive(window->selection->proxy, window->selection->mime, pipes[1]);
    close(pipes[1]);
    window->paste_fd = pipes[0];
    window->paste_deadline = window_input_now() + 30000000000ull;
}
static void wayland_transfer_poll(GrimWindow *window) {
    u64 now = window_input_now();
    u32 budget = WINDOW_TRANSFER_BUDGET;
    while (window->paste_fd >= 0 && budget) {
        u8 bytes[16384];
        ssize_t n = read(window->paste_fd, bytes, min(budget, sizeof(bytes)));
        if (n > 0) {
            if (!window_clipboard_append(&window->paste_bytes, &window->paste_len, &window->paste_cap, bytes, (u32)n)) {
                wayland_paste_complete(window, NULL, 0); break;
            }
            budget -= (u32)n;
            window->paste_deadline = now + 30000000000ull;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && errno == EAGAIN && now < window->paste_deadline) break;
        wayland_paste_complete(window, window->paste_bytes, n == 0 ? window->paste_len : 0);
    }
    budget = WINDOW_TRANSFER_BUDGET;
    WaylandWrite **link = &window->writes;
    while (*link) {
        WaylandWrite *t = *link;
        bool finished = now >= t->deadline || t->offset == t->copy->len;
        if (!finished && budget) {
            u32 count = min(t->copy->len - t->offset, budget);
            ssize_t n = window_input_pipe_write(t->fd, t->copy->bytes + t->offset, count);
            if (n > 0) { t->offset += (u32)n; budget -= (u32)n; t->deadline = now + 30000000000ull; }
            else if (n < 0 && errno != EAGAIN && errno != EINTR) finished = true;
            finished |= t->offset == t->copy->len;
        }
        if (finished) {
            *link = t->next; close(t->fd); window_clipboard_unref(t->copy); free(t);
        } else link = &t->next;
    }
}

static void wayland_repeat_stop(GrimWindow *window) {
    window->repeat_key = 0;
    window->repeat_next = 0;
    window->repeat_len = 0;
    window->repeat_composed = false;
}
static void wayland_key_emit(GrimWindow *window, u32 keycode, bool pressed, bool repeat) {
    if (!window->xkb_state) return;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(window->xkb_state, keycode);
    WindowKey key = window_input_key(sym);
    bool command = window_input_command(key, window->modifiers);
    if (key != WKEY_NONE && command) window_input_push(&window->events,
        (WindowEvent){.type = WINDOW_KEY, .key = key, .pressed = pressed, .modifiers = window->modifiers});
    if (!pressed) return;
    if (command) {
        if (window->compose) xkb_compose_state_reset(window->compose);
        return;
    }
    if (repeat && window->repeat_composed) {
        window_input_text(&window->events, window->repeat_text, window->repeat_len, window->modifiers);
        return;
    }
    bool composed = false;
    if (!repeat && window->compose && xkb_compose_state_feed(window->compose, sym) == XKB_COMPOSE_FEED_ACCEPTED) {
        enum xkb_compose_status status = xkb_compose_state_get_status(window->compose);
        if (status == XKB_COMPOSE_COMPOSING) return;
        if (status == XKB_COMPOSE_CANCELLED) { xkb_compose_state_reset(window->compose); return; }
        composed = status == XKB_COMPOSE_COMPOSED;
    }
    int size = composed ? xkb_compose_state_get_utf8(window->compose, NULL, 0) :
        xkb_state_key_get_utf8(window->xkb_state, keycode, NULL, 0);
    if (size > 0) {
        window_input_grow((void **)&window->repeat_text, &window->repeat_cap, (u32)size + 1, 1);
        if (composed) xkb_compose_state_get_utf8(window->compose, (char *)window->repeat_text, (size_t)size + 1);
        else xkb_state_key_get_utf8(window->xkb_state, keycode, (char *)window->repeat_text, (size_t)size + 1);
        if (window->repeat_text[0] >= 0x20 && window->repeat_text[0] != 0x7f) {
            window->repeat_len = (u32)size;
            window_input_text(&window->events, window->repeat_text, window->repeat_len, window->modifiers);
        }
    }
    window->repeat_composed = composed;
    if (composed) xkb_compose_state_reset(window->compose);
}
static void keyboard_keymap(void *data, struct wl_keyboard *keyboard, u32 format, i32 fd, u32 size) {
    (void)keyboard;
    GrimWindow *window = data;
    wayland_repeat_stop(window);
    if (window->compose) xkb_compose_state_reset(window->compose);
    struct xkb_keymap *keymap = NULL;
    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 && size && window->xkb_context) {
        char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map != MAP_FAILED) {
            if (map[size - 1] == '\0') keymap = xkb_keymap_new_from_string(window->xkb_context, map,
                XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
            munmap(map, size);
        }
    }
    close(fd);
    xkb_state_unref(window->xkb_state);
    xkb_keymap_unref(window->keymap);
    window->keymap = keymap;
    window->xkb_state = keymap ? xkb_state_new(keymap) : NULL;
    window->modifiers = 0;
    if (!window->xkb_state) fprintf(stderr, "Unable to load Wayland keyboard map\n");
}
static void keyboard_enter(void *data, struct wl_keyboard *keyboard, u32 serial, struct wl_surface *surface, struct wl_array *keys) {
    (void)keyboard; (void)surface; (void)keys;
    GrimWindow *window = data;
    window->focused = true;
    window->serial = serial;
    wayland_publish_clipboard(window);
}
static void keyboard_leave(void *data, struct wl_keyboard *keyboard, u32 serial, struct wl_surface *surface) {
    (void)keyboard; (void)serial; (void)surface;
    GrimWindow *window = data;
    window->focused = false;
    window->modifiers = 0;
    wayland_repeat_stop(window);
    if (window->compose) xkb_compose_state_reset(window->compose);
}
static void keyboard_key(void *data, struct wl_keyboard *keyboard, u32 serial, u32 time, u32 key, u32 state) {
    (void)keyboard; (void)time;
    GrimWindow *window = data;
    window->serial = serial;
    wayland_publish_clipboard(window);
    bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
    u32 keycode = key + 8; /* The Wayland xkb_v1 protocol defines this offset. */
    bool repeatable = window->keymap && xkb_keymap_key_repeats(window->keymap, keycode);
    if (pressed && repeatable) wayland_repeat_stop(window);
    wayland_key_emit(window, keycode, pressed, false);
    if (pressed && window->repeat_rate > 0 && repeatable) {
        /* A still-pending dead-key sequence must not emit uncomposed repeats. */
        if (!window->compose || xkb_compose_state_get_status(window->compose) != XKB_COMPOSE_COMPOSING) {
            window->repeat_key = keycode;
            window->repeat_next = window_input_now() + (u64)max(window->repeat_delay, 0) * 1000000ull;
        }
    } else if (!pressed && keycode == window->repeat_key) wayland_repeat_stop(window);
}
static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, u32 serial, u32 depressed, u32 latched, u32 locked, u32 group) {
    (void)keyboard; (void)serial;
    GrimWindow *window = data;
    if (!window->xkb_state) return;
    xkb_state_update_mask(window->xkb_state, depressed, latched, locked, 0, 0, group);
    u32 modifiers = 0;
    if (xkb_state_mod_name_is_active(window->xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0) modifiers |= WINDOW_SHIFT;
    if (xkb_state_mod_name_is_active(window->xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0) modifiers |= WINDOW_CTRL;
    if (xkb_state_mod_name_is_active(window->xkb_state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0) modifiers |= WINDOW_ALT;
    if (xkb_state_mod_name_is_active(window->xkb_state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0) modifiers |= WINDOW_SUPER;
    if (modifiers != window->modifiers) window->repeat_composed = false;
    window->modifiers = modifiers;
}
static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard, i32 rate, i32 delay) {
    (void)keyboard;
    GrimWindow *window = data;
    window->repeat_rate = rate;
    window->repeat_delay = delay;
    if (rate <= 0) wayland_repeat_stop(window);
}
static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap, .enter = keyboard_enter, .leave = keyboard_leave,
    .key = keyboard_key, .modifiers = keyboard_modifiers, .repeat_info = keyboard_repeat_info,
};
static void pointer_enter(void *data, struct wl_pointer *pointer, u32 serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
    (void)pointer; (void)surface;
    GrimWindow *window = data;
    window->serial = serial;
    window->pointer_x = (f32)wl_fixed_to_double(x);
    window->pointer_y = (f32)wl_fixed_to_double(y);
    window_input_push(&window->events, (WindowEvent){.type = WINDOW_POINTER_MOVE,
        .x = window->pointer_x, .y = window->pointer_y, .modifiers = window->modifiers});
    wayland_publish_clipboard(window);
}
static void pointer_leave(void *data, struct wl_pointer *pointer, u32 serial, struct wl_surface *surface) {
    (void)data; (void)pointer; (void)serial; (void)surface;
}
static void pointer_motion(void *data, struct wl_pointer *pointer, u32 time, wl_fixed_t x, wl_fixed_t y) {
    (void)pointer; (void)time;
    GrimWindow *window = data;
    f32 px = (f32)wl_fixed_to_double(x), py = (f32)wl_fixed_to_double(y);
    window_input_push(&window->events, (WindowEvent){.type = WINDOW_POINTER_MOVE,
        .x = px, .y = py, .dx = px - window->pointer_x, .dy = py - window->pointer_y, .modifiers = window->modifiers});
    window->pointer_x = px;
    window->pointer_y = py;
}
static void pointer_button(void *data, struct wl_pointer *pointer, u32 serial, u32 time, u32 button, u32 state) {
    (void)pointer; (void)time;
    GrimWindow *window = data;
    window->serial = serial;
    /* wl_pointer uses Linux button codes, not keyboard scancodes. */
    u32 mapped = button == 0x110 ? 1 : button == 0x112 ? 2 : button == 0x111 ? 3 : button;
    window_input_push(&window->events, (WindowEvent){.type = WINDOW_POINTER_BUTTON,
        .button = mapped, .pressed = state == WL_POINTER_BUTTON_STATE_PRESSED,
        .x = window->pointer_x, .y = window->pointer_y, .modifiers = window->modifiers});
    wayland_publish_clipboard(window);
}
static void pointer_axis(void *data, struct wl_pointer *pointer, u32 time, u32 axis, wl_fixed_t value) {
    (void)pointer; (void)time;
    GrimWindow *window = data;
    f32 amount = (f32)wl_fixed_to_double(value);
    window_input_push(&window->events, (WindowEvent){.type = WINDOW_SCROLL,
        .dx = axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL ? amount : 0,
        .dy = axis == WL_POINTER_AXIS_VERTICAL_SCROLL ? amount : 0,
        .x = window->pointer_x, .y = window->pointer_y, .modifiers = window->modifiers});
}
/* axis carries pixel distances already. Discrete/source/frame metadata must not
 * create a second scroll event for the same physical wheel movement. */
static void pointer_frame(void *data, struct wl_pointer *pointer) { (void)data; (void)pointer; }
static void pointer_axis_source(void *data, struct wl_pointer *pointer, u32 source) { (void)data; (void)pointer; (void)source; }
static void pointer_axis_stop(void *data, struct wl_pointer *pointer, u32 time, u32 axis) { (void)data; (void)pointer; (void)time; (void)axis; }
static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, u32 axis, i32 discrete) { (void)data; (void)pointer; (void)axis; (void)discrete; }
static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter, .leave = pointer_leave, .motion = pointer_motion, .button = pointer_button,
    .axis = pointer_axis, .frame = pointer_frame, .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop, .axis_discrete = pointer_axis_discrete,
};
static void seat_capabilities(void *data, struct wl_seat *seat, u32 capabilities) {
    GrimWindow *window = data;
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !window->keyboard) {
        window->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(window->keyboard, &keyboard_listener, window);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && window->keyboard) {
        wl_keyboard_destroy(window->keyboard);
        window->keyboard = NULL;
        window->focused = false;
        window->modifiers = 0;
        wayland_repeat_stop(window);
        if (window->compose) xkb_compose_state_reset(window->compose);
    }
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !window->pointer) {
        window->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(window->pointer, &pointer_listener, window);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && window->pointer) {
        wl_pointer_destroy(window->pointer);
        window->pointer = NULL;
    }
}
static void seat_name(void *data, struct wl_seat *seat, const char *name) { (void)data; (void)seat; (void)name; }
static const struct wl_seat_listener seat_listener = {.capabilities = seat_capabilities, .name = seat_name};
static void shell_ping(void *data, struct xdg_wm_base *shell, u32 serial) { (void)data; xdg_wm_base_pong(shell, serial); }
static const struct xdg_wm_base_listener shell_listener = {.ping = shell_ping};
static void registry_global(void *data, struct wl_registry *registry, u32 name, const char *interface, u32 version) {
    GrimWindow *window = data;
    if (!window->compositor && strcmp(interface, wl_compositor_interface.name) == 0) {
        window->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, min(version, 4));
        window->compositor_name = name;
    } else if (!window->xdg_base && strcmp(interface, xdg_wm_base_interface.name) == 0) {
        window->xdg_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        window->shell_name = name;
        xdg_wm_base_add_listener(window->xdg_base, &shell_listener, window);
    } else if (!window->seat && strcmp(interface, wl_seat_interface.name) == 0) {
        window->seat = wl_registry_bind(registry, name, &wl_seat_interface, min(version, 7));
        window->seat_name = name;
        wl_seat_add_listener(window->seat, &seat_listener, window);
        wayland_device_create(window);
    } else if (!window->data_manager && strcmp(interface, wl_data_device_manager_interface.name) == 0) {
        window->data_manager = wl_registry_bind(registry, name, &wl_data_device_manager_interface, min(version, 3));
        window->manager_name = name;
        wayland_device_create(window);
    }
}
static void wayland_device_destroy(GrimWindow *window) {
    while (window->offers) wayland_offer_destroy(window, window->offers);
    if (window->data_source) wl_data_source_destroy(window->data_source);
    if (window->data_device) {
        if (wl_data_device_get_version(window->data_device) >= 2) wl_data_device_release(window->data_device);
        else wl_data_device_destroy(window->data_device);
    }
    window->data_source = NULL;
    window->data_device = NULL;
    window->clipboard_pending = window->clipboard != NULL;
}
static void registry_remove(void *data, struct wl_registry *registry, u32 name) {
    (void)registry;
    GrimWindow *window = data;
    if (name == window->compositor_name || name == window->shell_name) window->request_close = true;
    if (name == window->seat_name && window->seat) {
        wayland_device_destroy(window);
        if (window->keyboard) wl_keyboard_destroy(window->keyboard);
        if (window->pointer) wl_pointer_destroy(window->pointer);
        wl_seat_destroy(window->seat);
        window->keyboard = NULL; window->pointer = NULL; window->seat = NULL;
        window->seat_name = 0; window->serial = 0; window->focused = false; window->modifiers = 0;
        wayland_repeat_stop(window);
    }
    if (name == window->manager_name && window->data_manager) {
        wayland_device_destroy(window);
        wl_data_device_manager_destroy(window->data_manager);
        window->data_manager = NULL; window->manager_name = 0;
    }
}
static const struct wl_registry_listener registry_listener = {.global = registry_global, .global_remove = registry_remove};
static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, i32 width, i32 height, struct wl_array *states) {
    (void)toplevel; (void)states;
    GrimWindow *window = data;
    if (width > 0) window->pending_width = (u32)width;
    if (height > 0) window->pending_height = (u32)height;
}
static void toplevel_close(void *data, struct xdg_toplevel *toplevel) { (void)toplevel; ((GrimWindow *)data)->request_close = true; }
static const struct xdg_toplevel_listener toplevel_listener = {.configure = toplevel_configure, .close = toplevel_close};
static void surface_configure(void *data, struct xdg_surface *surface, u32 serial) {
    GrimWindow *window = data;
    xdg_surface_ack_configure(surface, serial);
    window->width = window->pending_width;
    window->height = window->pending_height;
    xdg_surface_set_window_geometry(surface, 0, 0, (i32)window->width, (i32)window->height);
    window->configured = true;
}
static const struct xdg_surface_listener surface_listener = {.configure = surface_configure};

static void window_open(GrimWindow *window, u32 width, u32 height) {
    *window = (GrimWindow){.width = width, .height = height, .pending_width = width, .pending_height = height,
        .paste_fd = -1, .repeat_rate = 25, .repeat_delay = 600};
    const char *locale = setlocale(LC_CTYPE, "");
    window->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    window->compose = window_input_compose(window->xkb_context, locale);
    window->display = wl_display_connect(NULL);
    if (!window->display || !window->xkb_context) {
        fprintf(stderr, "Unable to initialize Wayland window and keyboard\n"); exit(EXIT_FAILURE);
    }
    window->registry = wl_display_get_registry(window->display);
    wl_registry_add_listener(window->registry, &registry_listener, window);
    if (wl_display_roundtrip(window->display) < 0 || !window->compositor || !window->xdg_base) {
        fprintf(stderr, "Wayland compositor must support wl_compositor and xdg-shell\n"); exit(EXIT_FAILURE);
    }
    window->surface = wl_compositor_create_surface(window->compositor);
    window->xdg_surface = xdg_wm_base_get_xdg_surface(window->xdg_base, window->surface);
    xdg_surface_add_listener(window->xdg_surface, &surface_listener, window);
    window->xdg_toplevel = xdg_surface_get_toplevel(window->xdg_surface);
    xdg_toplevel_add_listener(window->xdg_toplevel, &toplevel_listener, window);
    xdg_toplevel_set_app_id(window->xdg_toplevel, "dev-float");
    xdg_toplevel_set_title(window->xdg_toplevel, "rwmd");
}
void window_set_title(GrimWindow *window, const char *title) {
    xdg_toplevel_set_title(window->xdg_toplevel, title);
}
void window_set_fullscreen(GrimWindow *window) {
    xdg_toplevel_set_fullscreen(window->xdg_toplevel, NULL);
    if (window->configured) wl_surface_commit(window->surface);
}
static void window_prepare_render(GrimWindow *window) {
    wl_surface_commit(window->surface);
    while (!window->configured && !window->request_close) {
        if (wl_display_dispatch(window->display) < 0) {
            fprintf(stderr, "Wayland initial configure failed\n"); exit(EXIT_FAILURE);
        }
    }
}
/* Prepare/read must stay paired even when clipboard readiness wakes this wait.
 * Pending Wayland callbacks are dispatched only by window_poll_events. */
void window_wait_events(GrimWindow *window, u32 timeout_ms) {
    if (window->request_close) return;
    if (wl_display_prepare_read(window->display) != 0) return;
    int flushed = wl_display_flush(window->display);
    if (flushed < 0 && errno != EAGAIN) {
        wl_display_cancel_read(window->display); window->request_close = true; return;
    }
    u32 count = 1 + (window->paste_fd >= 0);
    for (WaylandWrite *t = window->writes; t; t = t->next) ++count;
    window_input_grow((void **)&window->wait_fds, &window->wait_cap, count, sizeof(*window->wait_fds));
    window->wait_fds[0] = (struct pollfd){.fd = wl_display_get_fd(window->display), .events = POLLIN | (flushed < 0 ? POLLOUT : 0)};
    u32 index = 1;
    u64 deadline = window->repeat_next;
    if (window->paste_fd >= 0) {
        window->wait_fds[index++] = (struct pollfd){.fd = window->paste_fd, .events = POLLIN};
        if (!deadline || window->paste_deadline < deadline) deadline = window->paste_deadline;
    }
    for (WaylandWrite *t = window->writes; t; t = t->next) {
        window->wait_fds[index++] = (struct pollfd){.fd = t->fd, .events = POLLOUT};
        if (!deadline || t->deadline < deadline) deadline = t->deadline;
    }
    int timeout = (int)min(timeout_ms, (u32)INT_MAX);
    u64 now = window_input_now();
    if (deadline) timeout = min(timeout, (int)min(deadline > now ? (deadline - now + 999999) / 1000000 : 0, (u64)INT_MAX));
    int result = poll(window->wait_fds, count, timeout);
    short revents = window->wait_fds[0].revents;
    if (result > 0 && (revents & POLLIN)) {
        if (wl_display_read_events(window->display) < 0) window->request_close = true;
    } else wl_display_cancel_read(window->display);
    if ((result < 0 && errno != EINTR) || (revents & (POLLERR | POLLHUP | POLLNVAL))) window->request_close = true;
    if (revents & POLLOUT) {
        if (wl_display_flush(window->display) < 0 && errno != EAGAIN) window->request_close = true;
    }
}
void window_poll_events(GrimWindow *window) {
    if (wl_display_dispatch_pending(window->display) < 0) { window->request_close = true; return; }
    /* Existing queued editor events must not prevent servicing the display. */
    window_wait_events(window, 0);
    if (wl_display_dispatch_pending(window->display) < 0) window->request_close = true;
    u64 now = window_input_now();
    if (window->repeat_key && window->repeat_rate > 0 && now >= window->repeat_next) {
        u64 interval = 1000000000ull / (u32)window->repeat_rate;
        if (!interval) interval = 1;
        /* Bound catch-up after an expensive frame without losing normal repeat. */
        u32 repeats = (u32)min(1 + (now - window->repeat_next) / interval, 32ull);
        for (u32 i = 0; i < repeats; ++i) wayland_key_emit(window, window->repeat_key, true, true);
        window->repeat_next += (u64)repeats * interval;
        if (window->repeat_next <= now) window->repeat_next = now + interval;
    }
    wayland_transfer_poll(window);
}
void close_window(GrimWindow *window) {
    wayland_paste_complete(window, NULL, 0);
    while (window->writes) {
        WaylandWrite *next = window->writes->next;
        close(window->writes->fd); window_clipboard_unref(window->writes->copy); free(window->writes);
        window->writes = next;
    }
    wayland_device_destroy(window);
    if (window->data_manager) wl_data_device_manager_destroy(window->data_manager);
    if (window->keyboard) wl_keyboard_destroy(window->keyboard);
    if (window->pointer) wl_pointer_destroy(window->pointer);
    if (window->seat) wl_seat_destroy(window->seat);
    if (window->xdg_toplevel) xdg_toplevel_destroy(window->xdg_toplevel);
    if (window->xdg_surface) xdg_surface_destroy(window->xdg_surface);
    if (window->surface) wl_surface_destroy(window->surface);
    if (window->xdg_base) xdg_wm_base_destroy(window->xdg_base);
    if (window->compositor) wl_compositor_destroy(window->compositor);
    if (window->registry) wl_registry_destroy(window->registry);
    if (window->display) { wl_display_flush(window->display); wl_display_disconnect(window->display); }
    xkb_compose_state_unref(window->compose);
    xkb_state_unref(window->xkb_state);
    xkb_keymap_unref(window->keymap);
    xkb_context_unref(window->xkb_context);
    window_clipboard_unref(window->clipboard);
    window_input_destroy(&window->events);
    free(window->paste_bytes); free(window->repeat_text); free(window->wait_fds);
    *window = (GrimWindow){.paste_fd = -1};
}
