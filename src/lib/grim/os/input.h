#pragma once

#include <lib/grim/bp.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>

typedef enum WindowEventType {
    WINDOW_TEXT, WINDOW_KEY, WINDOW_POINTER_MOVE, WINDOW_POINTER_BUTTON, WINDOW_SCROLL, WINDOW_PASTE
} WindowEventType;
typedef enum WindowKey {
    WKEY_NONE, WKEY_LEFT, WKEY_RIGHT, WKEY_UP, WKEY_DOWN, WKEY_HOME, WKEY_END,
    WKEY_PAGE_UP, WKEY_PAGE_DOWN, WKEY_BACKSPACE, WKEY_DELETE, WKEY_ENTER, WKEY_TAB,
    WKEY_ESCAPE, WKEY_A, WKEY_C, WKEY_V, WKEY_X, WKEY_Z, WKEY_Y, WKEY_S, WKEY_Q
} WindowKey;
enum { WINDOW_SHIFT = 1, WINDOW_CTRL = 2, WINDOW_ALT = 4, WINDOW_SUPER = 8 };
typedef struct WindowEvent {
    WindowEventType type;
    WindowKey key;
    u32 modifiers;
    bool pressed;
    u32 button;
    f32 x, y, dx, dy;
    u32 text_offset, text_len;
} WindowEvent;
typedef struct WindowEvents {
    WindowEvent *items;
    u32 count, cap;
    u8 *text;
    u32 text_len, text_cap;
} WindowEvents;

static void *window_input_alloc(size_t size) {
    void *p = malloc(size ? size : 1);
    if (!p) { fprintf(stderr, "Unable to allocate window input storage\n"); abort(); }
    return p;
}
static void window_input_grow(void **storage, u32 *capacity, u32 need, size_t element_size) {
    if (need <= *capacity) return;
    u32 cap = *capacity ? *capacity : 256;
    while (cap < need) {
        if (cap > UINT32_MAX / 2) { cap = need; break; }
        cap *= 2;
    }
    if ((size_t)cap > SIZE_MAX / element_size) {
        fprintf(stderr, "Window input storage overflow\n"); abort();
    }
    void *p = realloc(*storage, (size_t)cap * element_size);
    if (!p) { fprintf(stderr, "Unable to grow window input storage\n"); abort(); }
    *storage = p;
    *capacity = cap;
}
static void window_input_push(WindowEvents *queue, WindowEvent event) {
    if (queue->count == UINT32_MAX) { fprintf(stderr, "Window input queue overflow\n"); abort(); }
    window_input_grow((void **)&queue->items, &queue->cap, queue->count + 1, sizeof(*queue->items));
    queue->items[queue->count++] = event;
}
static void window_input_data(WindowEvents *queue, WindowEventType type, const u8 *text, u32 len, u32 modifiers) {
    if (len > UINT32_MAX - queue->text_len) {
        fprintf(stderr, "Window input text overflow\n"); abort();
    }
    window_input_grow((void **)&queue->text, &queue->text_cap, queue->text_len + len, 1);
    if (len) memcpy(queue->text + queue->text_len, text, len);
    window_input_push(queue, (WindowEvent){.type = type, .pressed = true,
        .modifiers = modifiers, .text_offset = queue->text_len, .text_len = len});
    queue->text_len += len;
}
static void window_input_text(WindowEvents *queue, const u8 *text, u32 len, u32 modifiers) {
    if (len) window_input_data(queue, WINDOW_TEXT, text, len, modifiers);
}
/* Completion is observable even when the selection is empty or unavailable. */
static void window_input_paste(WindowEvents *queue, const u8 *text, u32 len) {
    window_input_data(queue, WINDOW_PASTE, text, len, 0);
}
static void window_input_destroy(WindowEvents *queue) {
    free(queue->items);
    free(queue->text);
    *queue = (WindowEvents){0};
}
static WindowKey window_input_key(xkb_keysym_t sym) {
    switch (sym) {
        case XKB_KEY_Left: case XKB_KEY_KP_Left: return WKEY_LEFT;
        case XKB_KEY_Right: case XKB_KEY_KP_Right: return WKEY_RIGHT;
        case XKB_KEY_Up: case XKB_KEY_KP_Up: return WKEY_UP;
        case XKB_KEY_Down: case XKB_KEY_KP_Down: return WKEY_DOWN;
        case XKB_KEY_Home: case XKB_KEY_KP_Home: return WKEY_HOME;
        case XKB_KEY_End: case XKB_KEY_KP_End: return WKEY_END;
        case XKB_KEY_Page_Up: case XKB_KEY_KP_Page_Up: return WKEY_PAGE_UP;
        case XKB_KEY_Page_Down: case XKB_KEY_KP_Page_Down: return WKEY_PAGE_DOWN;
        case XKB_KEY_BackSpace: return WKEY_BACKSPACE;
        case XKB_KEY_Delete: case XKB_KEY_KP_Delete: return WKEY_DELETE;
        case XKB_KEY_Return: case XKB_KEY_KP_Enter: return WKEY_ENTER;
        case XKB_KEY_Tab: case XKB_KEY_ISO_Left_Tab: case XKB_KEY_KP_Tab: return WKEY_TAB;
        case XKB_KEY_Escape: return WKEY_ESCAPE;
        case XKB_KEY_a: case XKB_KEY_A: return WKEY_A;
        case XKB_KEY_c: case XKB_KEY_C: return WKEY_C;
        case XKB_KEY_v: case XKB_KEY_V: return WKEY_V;
        case XKB_KEY_x: case XKB_KEY_X: return WKEY_X;
        case XKB_KEY_z: case XKB_KEY_Z: return WKEY_Z;
        case XKB_KEY_y: case XKB_KEY_Y: return WKEY_Y;
        case XKB_KEY_s: case XKB_KEY_S: return WKEY_S;
        case XKB_KEY_q: case XKB_KEY_Q: return WKEY_Q;
        default: return WKEY_NONE;
    }
}
static bool window_input_command(WindowKey key, u32 modifiers) {
    return (key >= WKEY_LEFT && key <= WKEY_ESCAPE) ||
        (modifiers & (WINDOW_CTRL | WINDOW_ALT | WINDOW_SUPER));
}
static u64 window_input_now(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (u64)now.tv_sec * 1000000000ull + (u64)now.tv_nsec;
}
static struct xkb_compose_state *window_input_compose(struct xkb_context *context, const char *locale) {
    if (!context) return NULL;
    struct xkb_compose_table *table = xkb_compose_table_new_from_locale(context,
        locale ? locale : "C", XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (!table) return NULL;
    struct xkb_compose_state *state = xkb_compose_state_new(table, XKB_COMPOSE_STATE_NO_FLAGS);
    xkb_compose_table_unref(table);
    return state;
}

/* Immutable, reference-counted clipboard snapshots survive a subsequent copy. */
typedef struct WindowClipboard {
    u32 refs, len;
    u8 bytes[];
} WindowClipboard;
static WindowClipboard *window_clipboard_new(const u8 *bytes, u32 len) {
    if ((u64)len + sizeof(WindowClipboard) > SIZE_MAX) abort();
    WindowClipboard *copy = window_input_alloc(sizeof(*copy) + len);
    copy->refs = 1;
    copy->len = len;
    if (len) memcpy(copy->bytes, bytes, len);
    return copy;
}
static void window_clipboard_unref(WindowClipboard *copy) {
    if (copy && !--copy->refs) free(copy);
}
/* External selections are untrusted; refuse partial/oversized transfers. */
enum { WINDOW_CLIPBOARD_LIMIT = INT32_MAX, WINDOW_TRANSFER_BUDGET = 262144 };
static bool window_clipboard_append(u8 **bytes, u32 *len, u32 *cap, const u8 *src, u32 count) {
    if (count > WINDOW_CLIPBOARD_LIMIT - *len) return false;
    window_input_grow((void **)bytes, cap, *len + count, 1);
    if (count) memcpy(*bytes + *len, src, count);
    *len += count;
    return true;
}
/* A receiver may disappear while a clipboard pipe is writable. Do not change
 * the process-wide SIGPIPE disposition, and do not consume an older signal. */
static ssize_t window_input_pipe_write(int fd, const void *bytes, size_t len) {
    sigset_t block, old, pending;
    sigemptyset(&block);
    sigaddset(&block, SIGPIPE);
    if (sigprocmask(SIG_BLOCK, &block, &old) < 0) return -1;
    sigpending(&pending);
    bool had_signal = sigismember(&pending, SIGPIPE) == 1;
    ssize_t written = write(fd, bytes, len);
    int error = errno;
    if (written < 0 && error == EPIPE && !had_signal) {
        struct timespec zero = {0};
        while (sigtimedwait(&block, NULL, &zero) < 0 && errno == EINTR) {}
    }
    sigprocmask(SIG_SETMASK, &old, NULL);
    errno = error;
    return written;
}
