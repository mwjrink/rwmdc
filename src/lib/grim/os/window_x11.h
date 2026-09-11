#pragma once

#include <lib/grim/os/input.h>
#include <locale.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#pragma push_macro("internal")
#undef internal
#include <X11/XKBlib.h>
#pragma pop_macro("internal")
#include <X11/Xproto.h>

typedef struct X11Transfer {
    struct X11Transfer *next;
    Window requestor;
    Atom property, target;
    WindowClipboard *copy;
    u32 offset;
    long saved_mask;
    u64 deadline;
    bool ready;
} X11Transfer;
typedef struct GrimWindow {
    Display *display;
    Window x11_window;
    Atom atom_wm_delete_window, atom_wm_protocols;
    Atom clipboard_atom, targets_atom, utf8_atom, text_atom, plain_atom, incr_atom;
    Atom timestamp_atom, multiple_atom, pair_atom, paste_atom;
    Atom compound_atom;
    WindowClipboard *clipboard;
    Time clipboard_time, last_time;
    X11Transfer *transfers;
    Window paste_window;
    Atom paste_target, paste_type;
    bool paste_pending;
    bool paste_incr, paste_reading, paste_targets;
    u8 *paste_bytes;
    u32 paste_len, paste_cap, paste_offset;
    u64 paste_deadline;
    u32 transfer_chunk;
    XIM xim;
    XIC xic;
    struct xkb_context *xkb_context;
    struct xkb_compose_state *compose;
    unsigned int alt_mask, super_mask;
    bool detectable_repeat;
    bool xkb_available;
    int xkb_event_base;
    int (*previous_error_handler)(Display *, XErrorEvent *);
    f32 pointer_x, pointer_y;
    u32 width, height;
    bool request_close, mapped, fullscreen_requested, focused;
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
static int (*x11_previous_error_handler)(Display *, XErrorEvent *);
static int x11_error_handler(Display *display, XErrorEvent *event) {
    /* Selection requestors can disappear between their event and our reply. */
    if (event->error_code == BadWindow && (event->request_code == X_ChangeProperty ||
        event->request_code == X_GetWindowAttributes || event->request_code == X_ChangeWindowAttributes ||
        event->request_code == X_SendEvent || event->request_code == X_GetProperty ||
        event->request_code == X_DeleteProperty)) return 0;
    return x11_previous_error_handler ? x11_previous_error_handler(display, event) : 0;
}
static u32 x11_modifiers(GrimWindow *window, unsigned int state) {
    return ((state & ShiftMask) ? WINDOW_SHIFT : 0) | ((state & ControlMask) ? WINDOW_CTRL : 0) |
        ((state & window->alt_mask) ? WINDOW_ALT : 0) | ((state & window->super_mask) ? WINDOW_SUPER : 0);
}
static void x11_modifier_map(GrimWindow *window) {
    window->alt_mask = 0;
    window->super_mask = 0;
    XModifierKeymap *map = XGetModifierMapping(window->display);
    if (!map) return;
    for (int mod = 0; mod < 8; ++mod) {
        for (int i = 0; i < map->max_keypermod; ++i) {
            KeyCode code = map->modifiermap[mod * map->max_keypermod + i];
            if (!code) continue;
            KeySym sym = XkbKeycodeToKeysym(window->display, code, 0, 0);
            if (sym == XKB_KEY_Alt_L || sym == XKB_KEY_Alt_R || sym == XKB_KEY_Meta_L || sym == XKB_KEY_Meta_R)
                window->alt_mask |= 1u << mod;
            if (sym == XKB_KEY_Super_L || sym == XKB_KEY_Super_R || sym == XKB_KEY_Hyper_L || sym == XKB_KEY_Hyper_R)
                window->super_mask |= 1u << mod;
        }
    }
    XFreeModifiermap(map);
}
static long x11_event_mask(void) {
    return StructureNotifyMask | KeyPressMask | KeyReleaseMask | FocusChangeMask |
        PointerMotionMask | ButtonPressMask | ButtonReleaseMask | PropertyChangeMask;
}
static void x11_im_destroy(XIM im, XPointer data, XPointer call) {
    (void)im; (void)call;
    GrimWindow *window = (GrimWindow *)data;
    window->xim = NULL;
    window->xic = NULL;
}
static void x11_im_open(Display *display, XPointer data, XPointer call) {
    (void)call;
    GrimWindow *window = (GrimWindow *)data;
    if (window->xim || !window->x11_window) return;
    XIM im = XOpenIM(display, NULL, NULL, NULL);
    if (!im) return;
    XIMStyles *styles = NULL;
    XIMStyle style = 0;
    if (!XGetIMValues(im, XNQueryInputStyle, &styles, NULL) && styles) {
        for (unsigned short i = 0; i < styles->count_styles; ++i) {
            if (styles->supported_styles[i] == (XIMPreeditNothing | XIMStatusNothing)) {
                style = styles->supported_styles[i]; break;
            }
            if (styles->supported_styles[i] == (XIMPreeditNone | XIMStatusNone)) style = styles->supported_styles[i];
        }
        XFree(styles);
    }
    if (!style) { XCloseIM(im); return; }
    XIC ic = XCreateIC(im, XNInputStyle, style, XNClientWindow, window->x11_window,
        XNFocusWindow, window->x11_window, NULL);
    if (!ic) { XCloseIM(im); return; }
    window->xim = im;
    window->xic = ic;
    XIMCallback destroy = {.client_data = (XPointer)window, .callback = x11_im_destroy};
    XSetIMValues(im, XNDestroyCallback, &destroy, NULL);
    long filter = 0;
    XGetICValues(ic, XNFilterEvents, &filter, NULL);
    XSelectInput(display, window->x11_window, x11_event_mask() | filter);
    if (window->focused) XSetICFocus(ic);
}

static void x11_paste_cancel(GrimWindow *window) {
    if (window->paste_pending) window_input_paste(&window->events, NULL, 0);
    window->paste_pending = false;
    if (window->paste_window) XDestroyWindow(window->display, window->paste_window);
    window->paste_window = 0;
    window->paste_reading = false;
    window->paste_incr = false;
    window->paste_targets = false;
    window->paste_len = 0;
    window->paste_offset = 0;
    window->paste_type = None;
}
static bool x11_latin1_length(const WindowClipboard *copy, u32 *length) {
    u32 len = 0;
    for (u32 i = 0; i < copy->len; ++i) {
        u8 c = copy->bytes[i];
        if (c >= 0xc2 && c <= 0xc3 && i + 1 < copy->len && (copy->bytes[i + 1] & 0xc0) == 0x80) ++i;
        else if (c >= 0x80) return false;
        ++len;
    }
    *length = len;
    return true;
}
static WindowClipboard *x11_latin1(const WindowClipboard *copy) {
    u32 len;
    if (!x11_latin1_length(copy, &len)) return NULL;
    WindowClipboard *latin = window_input_alloc(sizeof(*latin) + len);
    latin->refs = 1;
    latin->len = len;
    u32 out = 0;
    for (u32 i = 0; i < copy->len; ++i) {
        u8 c = copy->bytes[i];
        if (c >= 0xc2 && c <= 0xc3) { c = (u8)(((c & 3) << 6) | (copy->bytes[i + 1] & 63)); ++i; }
        latin->bytes[out++] = c;
    }
    return latin;
}
static WindowClipboard *x11_compound_text(GrimWindow *window) {
    const WindowClipboard *copy = window->clipboard;
    /* Xlib's text-list conversion cannot represent an embedded NUL. */
    if (memchr(copy->bytes, 0, copy->len)) return NULL;
    char *text = window_input_alloc((size_t)copy->len + 1);
    memcpy(text, copy->bytes, copy->len);
    text[copy->len] = 0;
    XTextProperty property = {0};
    int result = Xutf8TextListToTextProperty(window->display, &text, 1, XCompoundTextStyle, &property);
    free(text);
    WindowClipboard *converted = NULL;
    if (result == Success && property.format == 8 && property.nitems <= WINDOW_CLIPBOARD_LIMIT)
        converted = window_clipboard_new(property.value, (u32)property.nitems);
    if (property.value) XFree(property.value);
    return converted;
}
static void x11_transfer_remove(GrimWindow *window, X11Transfer **link, bool destroyed) {
    X11Transfer *t = *link;
    *link = t->next;
    bool another = false;
    for (X11Transfer *other = window->transfers; other; other = other->next)
        if (other->requestor == t->requestor) { another = true; break; }
    if (!another && !destroyed) XSelectInput(window->display, t->requestor, t->saved_mask);
    window_clipboard_unref(t->copy);
    free(t);
}
static bool x11_selection_convert(GrimWindow *window, Window requestor, Atom target, Atom property) {
    if (!property || !window->clipboard) return false;
    if (target == window->targets_atom) {
        Atom targets[] = {window->targets_atom, window->timestamp_atom, window->multiple_atom,
            window->utf8_atom, window->plain_atom, window->text_atom, window->compound_atom, XA_STRING};
        u32 latin_len;
        int count = x11_latin1_length(window->clipboard, &latin_len) ? 8 : 7;
        XChangeProperty(window->display, requestor, property, XA_ATOM, 32, PropModeReplace,
            (unsigned char *)targets, count);
        return true;
    }
    if (target == window->timestamp_atom) {
        unsigned long timestamp = window->clipboard_time;
        XChangeProperty(window->display, requestor, property, XA_INTEGER, 32, PropModeReplace,
            (unsigned char *)&timestamp, 1);
        return true;
    }
    WindowClipboard *copy;
    if (target == XA_STRING) copy = x11_latin1(window->clipboard);
    else if (target == window->compound_atom) copy = x11_compound_text(window);
    else if (target == window->utf8_atom || target == window->plain_atom || target == window->text_atom) {
        copy = window->clipboard; ++copy->refs;
    } else return false;
    if (!copy) return false;
    Atom type = target == window->text_atom ? window->utf8_atom : target;
    if (copy->len <= window->transfer_chunk) {
        XChangeProperty(window->display, requestor, property, type, 8, PropModeReplace, copy->bytes, (int)copy->len);
        window_clipboard_unref(copy);
        return true;
    }
    long saved_mask = 0;
    bool watched = false;
    for (X11Transfer *t = window->transfers; t; t = t->next) {
        if (t->requestor == requestor && t->property == property) { window_clipboard_unref(copy); return false; }
        if (t->requestor == requestor) { saved_mask = t->saved_mask; watched = true; }
    }
    if (!watched) {
        XWindowAttributes attrs;
        if (!XGetWindowAttributes(window->display, requestor, &attrs)) { window_clipboard_unref(copy); return false; }
        saved_mask = attrs.your_event_mask;
        XSelectInput(window->display, requestor, saved_mask | PropertyChangeMask | StructureNotifyMask);
    }
    X11Transfer *transfer = window_input_alloc(sizeof(*transfer));
    *transfer = (X11Transfer){.next = window->transfers, .requestor = requestor, .property = property,
        .target = type, .copy = copy, .saved_mask = saved_mask, .deadline = window_input_now() + 30000000000ull};
    window->transfers = transfer;
    unsigned long length = copy->len;
    XChangeProperty(window->display, requestor, property, window->incr_atom, 32, PropModeReplace,
        (unsigned char *)&length, 1);
    return true;
}
static void x11_selection_request(GrimWindow *window, const XSelectionRequestEvent *request) {
    XEvent reply = {0};
    reply.xselection = (XSelectionEvent){.type = SelectionNotify, .display = window->display,
        .requestor = request->requestor, .selection = request->selection,
        .target = request->target, .property = None, .time = request->time};
    Atom property = request->property ? request->property : request->target;
    if (request->selection == window->clipboard_atom && window->clipboard &&
        (request->time == CurrentTime || !window->clipboard_time || (int32_t)(request->time - window->clipboard_time) >= 0)) {
        if (request->target == window->multiple_atom && request->property) {
            Atom type = None;
            int format = 0;
            unsigned long count = 0, after = 0;
            unsigned char *data = NULL;
            if (XGetWindowProperty(window->display, request->requestor, property, 0, 4096, False,
                window->pair_atom, &type, &format, &count, &after, &data) == Success &&
                type == window->pair_atom && format == 32 && !(count & 1) && !after) {
                Atom *pairs = (Atom *)data;
                for (unsigned long i = 0; i < count; i += 2) {
                    if (pairs[i + 1] == property || !x11_selection_convert(window, request->requestor, pairs[i], pairs[i + 1]))
                        pairs[i + 1] = None;
                }
                XChangeProperty(window->display, request->requestor, property, window->pair_atom, 32,
                    PropModeReplace, data, (int)count);
                reply.xselection.property = property;
            }
            if (data) XFree(data);
        } else if (x11_selection_convert(window, request->requestor, request->target, property)) {
            reply.xselection.property = property;
        }
    }
    XSendEvent(window->display, request->requestor, False, 0, &reply);
}
void window_clipboard_set(GrimWindow *window, const u8 *bytes, u32 len) {
    WindowClipboard *copy = window_clipboard_new(bytes, len);
    window_clipboard_unref(window->clipboard);
    window->clipboard = copy;
    window->clipboard_time = window->last_time;
    XSetSelectionOwner(window->display, window->clipboard_atom, window->x11_window,
        window->last_time ? window->last_time : CurrentTime);
    XFlush(window->display);
}
static void x11_paste_convert(GrimWindow *window, Atom target) {
    window->paste_target = target;
    window->paste_targets = target == window->targets_atom;
    window->paste_type = None;
    window->paste_offset = 0;
    window->paste_reading = false;
    XDeleteProperty(window->display, window->paste_window, window->paste_atom);
    XConvertSelection(window->display, window->clipboard_atom, target, window->paste_atom,
        window->paste_window, window->last_time ? window->last_time : CurrentTime);
    XFlush(window->display);
}
void window_clipboard_request(GrimWindow *window) {
    x11_paste_cancel(window);
    window->paste_pending = true;
    Window owner = XGetSelectionOwner(window->display, window->clipboard_atom);
    if (owner == None) { x11_paste_cancel(window); return; }
    if (owner == window->x11_window && window->clipboard) {
        window_input_paste(&window->events, window->clipboard->bytes, window->clipboard->len);
        window->paste_pending = false;
        return;
    }
    /* A separate requestor makes cancellation safe even with a late INCR owner. */
    window->paste_window = XCreateSimpleWindow(window->display, DefaultRootWindow(window->display), 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(window->display, window->paste_window, PropertyChangeMask);
    window->paste_deadline = window_input_now() + 30000000000ull;
    x11_paste_convert(window, window->targets_atom);
}
static void x11_selection_notify(GrimWindow *window, const XSelectionEvent *event) {
    if (!window->paste_window || event->requestor != window->paste_window ||
        event->selection != window->clipboard_atom || event->target != window->paste_target) return;
    if (event->property == None) {
        /* Older owners sometimes omit TARGETS, but still serve UTF8_STRING. */
        if (window->paste_targets) x11_paste_convert(window, window->utf8_atom);
        else if (window->paste_target == window->utf8_atom) x11_paste_convert(window, XA_STRING);
        else x11_paste_cancel(window);
        return;
    }
    if (event->property != window->paste_atom) return;
    window->paste_reading = true;
    window->paste_offset = 0;
}
static bool x11_paste_append(GrimWindow *window, const u8 *bytes, u32 len, Atom type) {
    if (!len) return true;
    if (type != XA_STRING) return window_clipboard_append(&window->paste_bytes,
        &window->paste_len, &window->paste_cap, bytes, len);
    u32 extra = len;
    for (u32 i = 0; i < len; ++i) extra += bytes[i] >= 0x80;
    if (extra > WINDOW_CLIPBOARD_LIMIT - window->paste_len) return false;
    window_input_grow((void **)&window->paste_bytes, &window->paste_cap, window->paste_len + extra, 1);
    u8 *out = window->paste_bytes + window->paste_len;
    for (u32 i = 0; i < len; ++i) {
        if (bytes[i] >= 0x80) { *out++ = 0xc0 | (bytes[i] >> 6); *out++ = 0x80 | (bytes[i] & 63); }
        else *out++ = bytes[i];
    }
    window->paste_len += extra;
    return true;
}
static bool x11_paste_decode(GrimWindow *window) {
    if (window->paste_type != window->compound_atom) return true;
    XTextProperty property = {.value = window->paste_bytes, .encoding = window->compound_atom,
        .format = 8, .nitems = window->paste_len};
    char **strings = NULL;
    int count = 0;
    int result = Xutf8TextPropertyToTextList(window->display, &property, &strings, &count);
    if (result != Success) { if (strings) XFreeStringList(strings); return false; }
    window->paste_len = 0;
    bool valid = true;
    for (int i = 0; i < count && valid; ++i) {
        size_t len = strlen(strings[i]);
        const u8 separator = 0;
        if (i) valid = window_clipboard_append(&window->paste_bytes, &window->paste_len,
            &window->paste_cap, &separator, 1);
        if (valid) valid = len <= WINDOW_CLIPBOARD_LIMIT && window_clipboard_append(&window->paste_bytes,
            &window->paste_len, &window->paste_cap, (const u8 *)strings[i], (u32)len);
    }
    if (strings) XFreeStringList(strings);
    return valid;
}
static void x11_paste_poll(GrimWindow *window) {
    u32 budget = WINDOW_TRANSFER_BUDGET;
    while (window->paste_window && window->paste_reading && budget) {
        Atom type = None;
        int format = 0;
        unsigned long count = 0, after = 0;
        unsigned char *bytes = NULL;
        int result = XGetWindowProperty(window->display, window->paste_window, window->paste_atom,
            window->paste_offset, min(budget, window->transfer_chunk) / 4, False, AnyPropertyType,
            &type, &format, &count, &after, &bytes);
        if (result != Success || type == None) {
            if (bytes) XFree(bytes);
            x11_paste_cancel(window); break;
        }
        if (type == window->incr_atom && !window->paste_incr && !window->paste_offset && !window->paste_targets) {
            bool valid = format == 32 && count == 1 && !after && ((unsigned long *)bytes)[0] <= WINDOW_CLIPBOARD_LIMIT;
            XFree(bytes);
            if (!valid) { x11_paste_cancel(window); break; }
            window->paste_incr = true;
            window->paste_reading = false;
            XDeleteProperty(window->display, window->paste_window, window->paste_atom);
            break;
        }
        if (window->paste_targets) {
            Atom target = None;
            if (format == 32 && type == XA_ATOM && !after) {
                Atom *targets = (Atom *)bytes;
                for (unsigned long i = 0; i < count; ++i) {
                    if (targets[i] == window->utf8_atom) { target = window->utf8_atom; break; }
                    if (targets[i] == window->plain_atom) target = window->plain_atom;
                    else if (targets[i] == XA_STRING && !target) target = XA_STRING;
                    else if (targets[i] == window->text_atom && !target) target = window->text_atom;
                    else if (targets[i] == window->compound_atom && !target) target = window->compound_atom;
                }
            }
            XFree(bytes);
            if (target) x11_paste_convert(window, target);
            else x11_paste_cancel(window);
            break;
        }
        bool valid = format == 8 && (type == window->utf8_atom || type == window->plain_atom ||
            type == XA_STRING || type == window->compound_atom) &&
            (!window->paste_type || window->paste_type == type) && count <= budget;
        if (!valid || !x11_paste_append(window, bytes, (u32)count, type)) {
            XFree(bytes); x11_paste_cancel(window); break;
        }
        window->paste_type = type;
        XFree(bytes);
        budget -= (u32)count;
        window->paste_deadline = window_input_now() + 30000000000ull;
        if (after) {
            if (!count || (count & 3) || window->paste_offset > UINT32_MAX - count / 4) {
                x11_paste_cancel(window); break;
            }
            window->paste_offset += (u32)(count / 4);
            continue;
        }
        XDeleteProperty(window->display, window->paste_window, window->paste_atom);
        window->paste_offset = 0;
        window->paste_reading = false;
        if (!window->paste_incr || !count) {
            if (x11_paste_decode(window)) {
                window_input_paste(&window->events, window->paste_bytes, window->paste_len);
                window->paste_pending = false;
            }
            x11_paste_cancel(window);
        }
    }
    if (window->paste_window && window_input_now() >= window->paste_deadline) x11_paste_cancel(window);
}
static void x11_transfer_property(GrimWindow *window, const XPropertyEvent *event) {
    if (event->window == window->paste_window && event->atom == window->paste_atom &&
        event->state == PropertyNewValue && window->paste_incr) {
        window->paste_reading = true;
        window->paste_offset = 0;
    }
    if (event->state != PropertyDelete) return;
    X11Transfer **link = &window->transfers;
    while (*link) {
        X11Transfer *t = *link;
        if (t->requestor == event->window && t->property == event->atom) {
            t->ready = true;
            break;
        }
        link = &t->next;
    }
}
static void x11_key(GrimWindow *window, XKeyEvent *event, bool pressed) {
    window->last_time = event->time;
    u32 modifiers = x11_modifiers(window, event->state);
    KeySym sym = NoSymbol;
    unsigned int consumed = 0;
    XkbLookupKeySym(window->display, (KeyCode)event->keycode, event->state, &consumed, &sym);
    WindowKey key = window_input_key((xkb_keysym_t)sym);
    bool command = window_input_command(key, modifiers);
    char stack[128], *text = stack;
    int len = 0;
    Status status = XLookupNone;
    if (pressed && window->xic) {
        /* XIM's optional keysym result must not replace the physical event's
         * state-aware command symbol, especially for XLookupChars/None. */
        KeySym text_sym = NoSymbol;
        len = Xutf8LookupString(window->xic, event, text, sizeof(stack) - 1, &text_sym, &status);
        if (status == XBufferOverflow && len > 0 && len < INT_MAX) {
            text = window_input_alloc((size_t)len + 1);
            len = Xutf8LookupString(window->xic, event, text, len + 1, &text_sym, &status);
        }
        if (status != XLookupChars && status != XLookupBoth) len = 0;
    }
    if (key != WKEY_NONE && command) window_input_push(&window->events,
        (WindowEvent){.type = WINDOW_KEY, .key = key, .pressed = pressed, .modifiers = modifiers});
    if (pressed && !command) {
        if (!window->xic) {
            bool composed = false, pending = false;
            if (window->compose && xkb_compose_state_feed(window->compose, (xkb_keysym_t)sym) == XKB_COMPOSE_FEED_ACCEPTED) {
                enum xkb_compose_status state = xkb_compose_state_get_status(window->compose);
                composed = state == XKB_COMPOSE_COMPOSED;
                pending = state == XKB_COMPOSE_COMPOSING || state == XKB_COMPOSE_CANCELLED;
                if (state == XKB_COMPOSE_CANCELLED) xkb_compose_state_reset(window->compose);
            }
            if (composed) {
                len = xkb_compose_state_get_utf8(window->compose, NULL, 0);
                if (len >= (int)sizeof(stack)) text = window_input_alloc((size_t)len + 1);
                xkb_compose_state_get_utf8(window->compose, text, (size_t)len + 1);
                xkb_compose_state_reset(window->compose);
            } else if (!pending) {
                int count = xkb_keysym_to_utf8((xkb_keysym_t)sym, stack, sizeof(stack));
                len = count > 0 ? count - 1 : 0;
            }
        }
        if (len > 0 && (u8)text[0] >= 0x20 && (u8)text[0] != 0x7f)
            window_input_text(&window->events, (const u8 *)text, (u32)len, modifiers);
    } else if (pressed && window->compose) xkb_compose_state_reset(window->compose);
    if (text != stack) free(text);
}
static void window_handle_event(GrimWindow *window, XEvent *event) {
    if (window->xkb_available && event->type == window->xkb_event_base) {
        XkbEvent *xkb = (XkbEvent *)event;
        if (xkb->any.xkb_type == XkbMapNotify) {
            XkbRefreshKeyboardMapping(&xkb->map);
            x11_modifier_map(window);
        } else if (xkb->any.xkb_type == XkbNewKeyboardNotify) {
            /* LibX11 invalidates its map on NewKeyboardNotify; the XKB symbol
             * lookups in modifier_map materialize that new core-keyboard map. */
            x11_modifier_map(window);
            if (window->compose) xkb_compose_state_reset(window->compose);
        }
        return;
    }
    if (XFilterEvent(event, None)) return;
    switch (event->type) {
        case MapNotify: if (event->xmap.window == window->x11_window) window->mapped = true; break;
        case ConfigureNotify:
            if (event->xconfigure.window == window->x11_window) {
                window->width = (u32)event->xconfigure.width;
                window->height = (u32)event->xconfigure.height;
            }
            break;
        case ClientMessage:
            if (event->xclient.window == window->x11_window && event->xclient.message_type == window->atom_wm_protocols &&
                event->xclient.format == 32 && (Atom)event->xclient.data.l[0] == window->atom_wm_delete_window)
                window->request_close = true;
            break;
        case DestroyNotify:
            if (event->xdestroywindow.window == window->x11_window) { window->request_close = true; window->x11_window = 0; }
            else {
                X11Transfer **link = &window->transfers;
                while (*link) {
                    if ((*link)->requestor == event->xdestroywindow.window) x11_transfer_remove(window, link, true);
                    else link = &(*link)->next;
                }
            }
            break;
        case FocusIn:
            if (event->xfocus.window == window->x11_window && event->xfocus.mode != NotifyGrab && event->xfocus.mode != NotifyUngrab) {
                window->focused = true;
                if (window->xic) XSetICFocus(window->xic);
            }
            break;
        case FocusOut:
            if (event->xfocus.window == window->x11_window && event->xfocus.mode != NotifyGrab && event->xfocus.mode != NotifyUngrab) {
                window->focused = false;
                if (window->xic) { XUnsetICFocus(window->xic); char *pending = Xutf8ResetIC(window->xic); if (pending) XFree(pending); }
                if (window->compose) xkb_compose_state_reset(window->compose);
            }
            break;
        case MappingNotify:
            XRefreshKeyboardMapping(&event->xmapping);
            x11_modifier_map(window);
            break;
        case KeyPress: x11_key(window, &event->xkey, true); break;
        case KeyRelease:
            if (!window->detectable_repeat && XPending(window->display)) {
                XEvent next;
                XPeekEvent(window->display, &next);
                if (next.type == KeyPress && next.xkey.keycode == event->xkey.keycode && next.xkey.time == event->xkey.time) break;
            }
            x11_key(window, &event->xkey, false);
            break;
        case MotionNotify:
            window_input_push(&window->events, (WindowEvent){.type = WINDOW_POINTER_MOVE,
                .x = (f32)event->xmotion.x, .y = (f32)event->xmotion.y,
                .dx = event->xmotion.x - window->pointer_x, .dy = event->xmotion.y - window->pointer_y,
                .modifiers = x11_modifiers(window, event->xmotion.state)});
            window->pointer_x = (f32)event->xmotion.x;
            window->pointer_y = (f32)event->xmotion.y;
            window->last_time = event->xmotion.time;
            break;
        case ButtonPress: case ButtonRelease: {
            XButtonEvent *button = &event->xbutton;
            window->last_time = button->time;
            window->pointer_x = (f32)button->x;
            window->pointer_y = (f32)button->y;
            bool pressed = event->type == ButtonPress;
            u32 modifiers = x11_modifiers(window, button->state);
            if (button->button >= 4 && button->button <= 7) {
                /* Core X11 wheel detents have no pixel valuator; normalize to pixels. */
                if (pressed) window_input_push(&window->events, (WindowEvent){.type = WINDOW_SCROLL,
                    .dx = button->button == 6 ? -48 : button->button == 7 ? 48 : 0,
                    .dy = button->button == 4 ? -48 : button->button == 5 ? 48 : 0,
                    .x = window->pointer_x, .y = window->pointer_y, .modifiers = modifiers});
            } else window_input_push(&window->events, (WindowEvent){.type = WINDOW_POINTER_BUTTON,
                .button = button->button, .pressed = pressed, .x = window->pointer_x, .y = window->pointer_y, .modifiers = modifiers});
            break;
        }
        case SelectionRequest: x11_selection_request(window, &event->xselectionrequest); break;
        case SelectionNotify: x11_selection_notify(window, &event->xselection); break;
        case SelectionClear:
            /* In-flight INCR replies retain the previous immutable snapshot. */
            if (event->xselectionclear.selection == window->clipboard_atom &&
                (!window->clipboard_time || (int32_t)(event->xselectionclear.time - window->clipboard_time) >= 0)) {
                window_clipboard_unref(window->clipboard); window->clipboard = NULL;
            }
            break;
        case PropertyNotify: x11_transfer_property(window, &event->xproperty); break;
    }
}

static void window_open(GrimWindow *window, u32 width, u32 height) {
    *window = (GrimWindow){.width = width, .height = height};
    const char *locale = setlocale(LC_CTYPE, "");
    XSetLocaleModifiers("");
    window->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    window->compose = window_input_compose(window->xkb_context, locale);
    window->display = XOpenDisplay(NULL);
    if (!window->display) { fprintf(stderr, "Unable to open X11 display\n"); exit(EXIT_FAILURE); }
    window->previous_error_handler = XSetErrorHandler(x11_error_handler);
    x11_previous_error_handler = window->previous_error_handler;
    int screen = DefaultScreen(window->display);
    window->x11_window = XCreateSimpleWindow(window->display, RootWindow(window->display, screen),
        0, 0, width, height, 0, BlackPixel(window->display, screen), BlackPixel(window->display, screen));
    const char *names[] = {"WM_DELETE_WINDOW", "WM_PROTOCOLS", "CLIPBOARD", "TARGETS", "UTF8_STRING", "TEXT",
        "text/plain;charset=utf-8", "INCR", "TIMESTAMP", "MULTIPLE", "ATOM_PAIR", "RWMD_PASTE", "COMPOUND_TEXT"};
    Atom atoms[13];
    XInternAtoms(window->display, (char **)names, 13, False, atoms);
    window->atom_wm_delete_window = atoms[0]; window->atom_wm_protocols = atoms[1];
    window->clipboard_atom = atoms[2]; window->targets_atom = atoms[3]; window->utf8_atom = atoms[4];
    window->text_atom = atoms[5]; window->plain_atom = atoms[6]; window->incr_atom = atoms[7];
    window->timestamp_atom = atoms[8]; window->multiple_atom = atoms[9]; window->pair_atom = atoms[10]; window->paste_atom = atoms[11];
    window->compound_atom = atoms[12];
    XSetWMProtocols(window->display, window->x11_window, &window->atom_wm_delete_window, 1);
    XSelectInput(window->display, window->x11_window, x11_event_mask());
    XStoreName(window->display, window->x11_window, "rwmd");
    XClassHint class_hint = {.res_name = "rwmd", .res_class = "dev-float"};
    XSetClassHint(window->display, window->x11_window, &class_hint);
    int xkb_opcode, xkb_error, xkb_major = XkbMajorVersion, xkb_minor = XkbMinorVersion;
    window->xkb_available = XkbQueryExtension(window->display, &xkb_opcode, &window->xkb_event_base,
        &xkb_error, &xkb_major, &xkb_minor);
    if (window->xkb_available) {
        unsigned int events = XkbMapNotifyMask | XkbNewKeyboardNotifyMask;
        XkbSelectEvents(window->display, XkbUseCoreKbd, events, events);
    }
    Bool detectable = False;
    XkbSetDetectableAutoRepeat(window->display, True, &detectable);
    window->detectable_repeat = detectable;
    x11_modifier_map(window);
    window->transfer_chunk = (u32)min((XMaxRequestSize(window->display) - 64) * 4, 65536l);
    x11_im_open(window->display, (XPointer)window, NULL);
    XRegisterIMInstantiateCallback(window->display, NULL, NULL, NULL, x11_im_open, (XPointer)window);
}
void window_set_title(GrimWindow *window, const char *title) {
    XStoreName(window->display, window->x11_window, title);
    Atom name = XInternAtom(window->display, "_NET_WM_NAME", False);
    XChangeProperty(window->display, window->x11_window, name, window->utf8_atom, 8, PropModeReplace,
        (const unsigned char *)title, (int)strlen(title));
    XFlush(window->display);
}
void window_set_fullscreen(GrimWindow *window) {
    window->fullscreen_requested = true;
    Atom state = XInternAtom(window->display, "_NET_WM_STATE", False);
    Atom fullscreen = XInternAtom(window->display, "_NET_WM_STATE_FULLSCREEN", False);
    if (!window->mapped) XChangeProperty(window->display, window->x11_window, state, XA_ATOM, 32,
        PropModeReplace, (unsigned char *)&fullscreen, 1);
    else {
        XEvent event = {0};
        event.xclient.type = ClientMessage;
        event.xclient.window = window->x11_window;
        event.xclient.message_type = state;
        event.xclient.format = 32;
        event.xclient.data.l[0] = 1;
        event.xclient.data.l[1] = (long)fullscreen;
        event.xclient.data.l[3] = 1;
        XSendEvent(window->display, DefaultRootWindow(window->display), False,
            SubstructureRedirectMask | SubstructureNotifyMask, &event);
    }
    XFlush(window->display);
}
void window_poll_events(GrimWindow *window) {
    /* A producer flooding X events must not indefinitely starve rendering. */
    u32 budget = 4096;
    while (budget-- && XPending(window->display)) {
        XEvent event;
        XNextEvent(window->display, &event);
        window_handle_event(window, &event);
    }
    x11_paste_poll(window);
    u64 now = window_input_now();
    budget = WINDOW_TRANSFER_BUDGET;
    X11Transfer **link = &window->transfers;
    while (*link) {
        X11Transfer *t = *link;
        if (now >= t->deadline) { x11_transfer_remove(window, link, false); continue; }
        if (t->ready && budget) {
            u32 len = min(t->copy->len - t->offset, min(window->transfer_chunk, budget));
            XChangeProperty(window->display, t->requestor, t->property, t->target, 8, PropModeReplace,
                t->copy->bytes + t->offset, (int)len);
            t->offset += len;
            t->ready = false;
            t->deadline = now + 30000000000ull;
            budget -= len;
            if (!len) { x11_transfer_remove(window, link, false); continue; }
        }
        link = &t->next;
    }
    XFlush(window->display);
}
void window_wait_events(GrimWindow *window, u32 timeout_ms) {
    if (window->request_close || window->paste_reading || XPending(window->display)) return;
    u64 now = window_input_now(), deadline = window->paste_window ? window->paste_deadline : 0;
    for (X11Transfer *t = window->transfers; t; t = t->next) {
        if (t->ready) return;
        if (!deadline || t->deadline < deadline) deadline = t->deadline;
    }
    int timeout = (int)min(timeout_ms, (u32)INT_MAX);
    if (deadline) timeout = min(timeout, (int)min(deadline > now ? (deadline - now + 999999) / 1000000 : 0, (u64)INT_MAX));
    struct pollfd fd = {.fd = ConnectionNumber(window->display), .events = POLLIN};
    int result = poll(&fd, 1, timeout);
    if ((result < 0 && errno != EINTR) || (fd.revents & (POLLERR | POLLHUP | POLLNVAL))) window->request_close = true;
}
static void window_prepare_render(GrimWindow *window) {
    XMapRaised(window->display, window->x11_window);
    while (!window->mapped && !window->request_close) {
        XEvent event;
        XNextEvent(window->display, &event);
        window_handle_event(window, &event);
    }
    window_poll_events(window);
    if (window->fullscreen_requested) window_set_fullscreen(window);
    XWindowAttributes attributes;
    if (window->x11_window && XGetWindowAttributes(window->display, window->x11_window, &attributes)) {
        window->width = (u32)attributes.width;
        window->height = (u32)attributes.height;
    }
}
void close_window(GrimWindow *window) {
    x11_paste_cancel(window);
    while (window->transfers) x11_transfer_remove(window, &window->transfers, false);
    XUnregisterIMInstantiateCallback(window->display, NULL, NULL, NULL, x11_im_open, (XPointer)window);
    if (window->xic) XDestroyIC(window->xic);
    if (window->xim) XCloseIM(window->xim);
    if (window->x11_window) XDestroyWindow(window->display, window->x11_window);
    if (window->display) XCloseDisplay(window->display);
    XSetErrorHandler(window->previous_error_handler);
    xkb_compose_state_unref(window->compose);
    xkb_context_unref(window->xkb_context);
    window_clipboard_unref(window->clipboard);
    window_input_destroy(&window->events);
    free(window->paste_bytes);
    *window = (GrimWindow){0};
}
