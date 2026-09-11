#pragma once

#include <lib/grim/bp.h>
#include <stdalign.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utf8proc.h>

typedef struct ByteSpan { const u8 *data; u32 len; } ByteSpan;

typedef struct EditBuffer {
    alignas(64) u8 active[60];
    i32 active_cursor;
    u8 *before, *after;
    u32 before_cap, before_len, after_cap, after_len, active_len;
} EditBuffer;

_Static_assert(offsetof(EditBuffer, active_cursor) == 60, "hot prefix");
_Static_assert(offsetof(EditBuffer, before) == 64, "cold metadata");
_Static_assert(sizeof(EditBuffer) == 128, "aligned object size");

typedef struct DocumentEdit {
    u32 offset, removed_len;
    ByteSpan inserted;
    u64 revision;
} DocumentEdit;

typedef struct DocumentUndo {
    u32 offset, removed_len, inserted_len, cursor_before, cursor_after;
    u8 *payload; // Removed bytes followed by inserted bytes; never borrowed storage.
    u64 state_before, state_after; // Undo-state identity, independent of invalidation revision.
} DocumentUndo;

typedef struct DocumentHistory {
    DocumentUndo *items;
    u32 len, position, cap;
    char error[256]; // Separate allocation permits diagnostics through document_save's const handle.
} DocumentHistory;

typedef struct Document {
    EditBuffer text;
    u64 revision;
    u64 state_id;
    bool crlf; // Loaded newline preference; edits and saving never rewrite source line endings.
    DocumentEdit last_edit;
    DocumentHistory *history;
    const char *initial_error;
} Document;

internal u32 document_length(const Document *d) {
    return d->text.before_len + d->text.active_len + d->text.after_len;
}

internal u32 document_cursor(const Document *d) {
    return (u32)((i64)d->text.before_len + d->text.active_cursor);
}

internal u64 document_state_id(const Document *d) { return d->state_id; }

internal const char *document_error(const Document *d) {
    if (!d) return "No document";
    return d->history ? d->history->error : (d->initial_error ? d->initial_error : "");
}

internal bool document_fail(const Document *d, const char *message) {
    if (d && d->history) snprintf(d->history->error, sizeof(d->history->error), "%s", message);
    return false;
}

internal bool document_io_fail(const Document *d, const char *operation, int error) {
    if (d && d->history)
        snprintf(d->history->error, sizeof(d->history->error), "%s: %s", operation, strerror(error));
    errno = error;
    return false;
}

internal bool document_history_ready(Document *d) {
    if (d->history) return true;
    d->history = calloc(1, sizeof(*d->history));
    d->initial_error = d->history ? NULL : "Cannot allocate document history";
    return d->history != NULL;
}

// Invalid ranges produce no spans, and never overflow offset + len.
internal u32 document_spans(const Document *d, u32 offset, u32 len, ByteSpan out[3]) {
    u32 length = document_length(d);
    if (!out || offset > length || len > length - offset) {
        document_fail(d, "Span range outside document");
        return 0;
    }
    if (!len) return 0;
    const EditBuffer *b = &d->text;
    ByteSpan regions[3] = {
        {b->before, b->before_len}, {b->active, b->active_len},
        {b->after_len ? b->after + b->after_cap - b->after_len : NULL, b->after_len}
    };
    u32 count = 0;
    for (u32 i = 0; i < 3 && len; ++i) {
        if (offset >= regions[i].len) { offset -= regions[i].len; continue; }
        u32 take = min(len, regions[i].len - offset);
        out[count++] = (ByteSpan){regions[i].data + offset, take};
        len -= take;
        offset = 0;
    }
    return count;
}

internal void document_read(const Document *d, u32 offset, u32 len, u8 *dest) {
    if (!dest && len) { document_fail(d, "Missing read destination"); return; }
    if (offset > document_length(d) || len > document_length(d) - offset) {
        document_fail(d, "Read range outside document"); return;
    }
    ByteSpan spans[3];
    u32 count = document_spans(d, offset, len, spans);
    for (u32 i = 0; i < count; ++i) {
        memcpy(dest, spans[i].data, spans[i].len);
        dest += spans[i].len;
    }
}

internal u8 document_byte(const Document *d, u32 offset) {
    const EditBuffer *b = &d->text;
    if (offset < b->before_len) return b->before[offset];
    offset -= b->before_len;
    if (offset < b->active_len) return b->active[offset];
    return b->after[b->after_cap - b->after_len + offset - b->active_len];
}

internal bool document_utf8_valid(const u8 *bytes, u32 len) {
    if (!bytes && len) return false;
    for (u32 i = 0; i < len;) {
        if (bytes[i] < 0x80) { ++i; continue; }
        utf8proc_int32_t cp;
        utf8proc_ssize_t n = utf8proc_iterate(bytes + i, len - i, &cp);
        if (n <= 0) return false;
        i += (u32)n;
    }
    return true;
}

internal bool document_codepoint_boundary(const Document *d, u32 pos) {
    return pos == document_length(d) || (document_byte(d, pos) & 0xc0) != 0x80;
}

internal u32 document_decode(const Document *d, u32 pos, utf8proc_int32_t *cp) {
    u8 bytes[4];
    u32 n = min(4u, document_length(d) - pos);
    document_read(d, pos, n, bytes);
    utf8proc_ssize_t used = utf8proc_iterate(bytes, n, cp);
    return used > 0 ? (u32)used : 1; // Source validation guarantees success.
}

internal u32 document_line_start(const Document *d, u32 pos) {
    pos = min(pos, document_length(d));
    // A position inside CRLF belongs to the preceding hard line.
    if (pos && pos < document_length(d) && document_byte(d, pos - 1) == '\r' && document_byte(d, pos) == '\n') --pos;
    while (pos) {
        u8 c = document_byte(d, pos - 1);
        if (c == '\r' || c == '\n') break;
        --pos;
    }
    return pos;
}

internal u32 document_line_end(const Document *d, u32 pos) {
    u32 len = document_length(d);
    pos = min(pos, len);
    if (pos && pos < len && document_byte(d, pos - 1) == '\r' && document_byte(d, pos) == '\n') --pos;
    while (pos < len && document_byte(d, pos) != '\r' && document_byte(d, pos) != '\n') ++pos;
    return pos;
}

// Unicode break state is replayed from a hard-line boundary, never from an arbitrary
// code point: RI parity, emoji ZWJ and Indic conjunct state require left context.
internal u32 document_grapheme_scan(const Document *d, u32 pos, bool next) {
    u32 len = document_length(d), start = document_line_start(d, pos), previous = start;
    if (!next && start == pos && pos) {
        start = document_line_start(d, pos - 1);
        previous = start;
    }
    if (start == len) return len;
    utf8proc_int32_t left, right, state = 0;
    u32 cursor = start + document_decode(d, start, &left);
    while (cursor < len) {
        u32 width = document_decode(d, cursor, &right);
        if (utf8proc_grapheme_break_stateful(left, right, &state)) {
            if (next && cursor > pos) return cursor;
            if (!next && cursor >= pos) return previous;
            previous = cursor;
        }
        cursor += width;
        left = right;
    }
    return next ? len : previous;
}

internal u32 document_prev_grapheme(const Document *d, u32 pos) {
    pos = min(pos, document_length(d));
    if (!pos) return 0;
    u8 c = document_byte(d, pos - 1);
    if (c < 0x80 && (pos == 1 || document_byte(d, pos - 2) < 0x80) &&
        !(c == '\n' && pos > 1 && document_byte(d, pos - 2) == '\r')) return pos - 1;
    if (c == '\n' && pos > 1 && document_byte(d, pos - 2) == '\r') return pos - 2;
    return document_grapheme_scan(d, pos, false);
}

internal u32 document_next_grapheme(const Document *d, u32 pos) {
    u32 len = document_length(d);
    if (pos >= len) return len;
    u8 c = document_byte(d, pos);
    if (c < 0x80) {
        if (pos + 1 == len) return len;
        u8 right = document_byte(d, pos + 1);
        if (right < 0x80) return pos + ((c == '\r' && right == '\n') ? 2 : 1);
    }
    return document_grapheme_scan(d, pos, true);
}

internal u32 document_snap_cursor(const Document *d, u32 pos, bool forward) {
    u32 len = document_length(d);
    pos = min(pos, len);
    if (!pos || pos == len) return pos;
    u32 previous = document_prev_grapheme(d, pos);
    u32 end = document_next_grapheme(d, previous);
    return end == pos ? pos : (forward ? end : previous);
}

internal void document_set_cursor(Document *d, u32 pos) {
    pos = document_snap_cursor(d, pos, false);
    d->text.active_cursor = (i32)((i64)pos - d->text.before_len);
}

internal void document_destroy(Document *d) {
    if (!d) return;
    free(d->text.before);
    free(d->text.after);
    if (d->history) {
        for (u32 i = 0; i < d->history->len; ++i) free(d->history->items[i].payload);
        free(d->history->items);
        free(d->history);
    }
    memset(d, 0, sizeof(*d));
}

internal void document_detect_crlf(Document *d, const u8 *bytes, u32 len) {
    d->crlf = false;
    for (u32 i = 1; i < len; ++i) if (bytes[i - 1] == '\r' && bytes[i] == '\n') { d->crlf = true; break; }
}

// Initialize fresh storage (including on failure). Use load, not init, to replace a live document.
internal bool document_init(Document *d, const u8 *bytes, u32 len) {
    if (!d) return false;
    memset(d, 0, sizeof(*d));
    if (!document_history_ready(d)) return false;
    if (len > INT32_MAX) return document_fail(d, "Document exceeds INT32_MAX bytes");
    if (!document_utf8_valid(bytes, len)) return document_fail(d, "Document is not valid UTF-8");
    if (len) {
        d->text.after = malloc(len);
        if (!d->text.after) return document_fail(d, "Cannot allocate document text");
        memcpy(d->text.after, bytes, len);
        d->text.after_cap = d->text.after_len = len;
    }
    document_detect_crlf(d, bytes, len);
    return true;
}

internal u32 document_grown_capacity(u32 current, u32 needed) {
    u64 cap = current ? current : 256;
    while (cap < needed) cap = min((u64)INT32_MAX, cap + cap / 2 + 1);
    return (u32)cap;
}

internal bool document_reserve_region(Document *d, bool after, u32 needed) {
    EditBuffer *b = &d->text;
    u32 old_cap = after ? b->after_cap : b->before_cap;
    if (needed <= old_cap) return true;
    if (needed > INT32_MAX) return document_fail(d, "Document capacity exceeds INT32_MAX");
    u32 cap = document_grown_capacity(old_cap, needed);
    u8 *bytes = realloc(after ? b->after : b->before, cap);
    if (!bytes) return document_fail(d, "Cannot grow document text");
    if (after) {
        if (b->after_len) memmove(bytes + cap - b->after_len, bytes + old_cap - b->after_len, b->after_len);
        b->after = bytes;
        b->after_cap = cap;
    } else {
        b->before = bytes;
        b->before_cap = cap;
    }
    return true;
}

// All allocations precede byte movement. On failure logical source and caret survive.
internal bool document_materialize(Document *d, u32 offset, u32 bulk_insert) {
    EditBuffer *b = &d->text;
    u32 len = document_length(d), before = b->before_len, active = b->active_len;
    u32 needed_before = offset + bulk_insert;
    if (!document_reserve_region(d, false, needed_before) ||
        !document_reserve_region(d, true, len - offset)) return false;
    if (offset < before) {
        u32 moved = before - offset;
        u8 *target = b->after + b->after_cap - b->after_len - active - moved;
        memcpy(target, b->before + offset, moved);
        if (active) memcpy(target + moved, b->active, active);
        b->after_len += moved + active;
    } else if (offset <= before + active) {
        u32 prefix = offset - before, suffix = active - prefix;
        if (prefix) memcpy(b->before + before, b->active, prefix);
        if (suffix) memcpy(b->after + b->after_cap - b->after_len - suffix, b->active + prefix, suffix);
        b->after_len += suffix;
    } else {
        u32 moved = offset - before - active;
        if (active) memcpy(b->before + before, b->active, active);
        memcpy(b->before + before + active, b->after + b->after_cap - b->after_len, moved);
        b->after_len -= moved;
    }
    b->before_len = offset;
    b->active_len = 0;
    b->active_cursor = 0;
    return true;
}

internal bool document_apply_bytes(Document *d, u32 offset, u32 removed, const u8 *bytes, u32 inserted) {
    EditBuffer *b = &d->text;
    u32 active_end = b->before_len + b->active_len;
    if (offset >= b->before_len && offset <= active_end && removed == active_end - offset &&
        (u64)(offset - b->before_len) + inserted <= sizeof(b->active)) {
        b->active_len = offset - b->before_len;
        if (inserted) memcpy(b->active + b->active_len, bytes, inserted);
        b->active_len += inserted;
    } else {
        if (!document_materialize(d, offset, inserted > sizeof(b->active) ? inserted : 0)) return false;
        b->after_len -= removed;
        if (inserted > sizeof(b->active)) {
            memcpy(b->before + b->before_len, bytes, inserted);
            b->before_len += inserted;
        } else {
            if (inserted) memcpy(b->active, bytes, inserted);
            b->active_len = inserted;
        }
    }
    u32 caret = document_snap_cursor(d, offset + inserted, true);
    b->active_cursor = (i32)((i64)caret - b->before_len);
    return true;
}

internal bool document_history_reserve(Document *d) {
    DocumentHistory *h = d->history;
    if (h->position < h->cap) return true;
    u64 cap = h->cap ? (u64)h->cap * 2 : 32;
    if (cap > UINT32_MAX || cap > SIZE_MAX / sizeof(*h->items)) return document_fail(d, "Undo history is too large");
    DocumentUndo *items = realloc(h->items, (usize)cap * sizeof(*items));
    if (!items) return document_fail(d, "Cannot grow undo history");
    h->items = items;
    h->cap = (u32)cap;
    return true;
}

internal bool document_replace(Document *d, u32 offset, u32 removed, const u8 *bytes, u32 inserted) {
    if (!d || !document_history_ready(d)) return false;
    u32 len = document_length(d);
    if (offset > len || removed > len - offset) return document_fail(d, "Edit range outside document");
    if ((u64)len - removed + inserted > INT32_MAX) return document_fail(d, "Document exceeds INT32_MAX bytes");
    if (!document_codepoint_boundary(d, offset) || !document_codepoint_boundary(d, offset + removed))
        return document_fail(d, "Edit splits a UTF-8 code point");
    if (!document_utf8_valid(bytes, inserted)) return document_fail(d, "Inserted text is not valid UTF-8");
    if (!removed && !inserted) { d->history->error[0] = 0; return true; }
    if (d->revision == UINT64_MAX) return document_fail(d, "Document revision overflow");
    if (!document_history_reserve(d)) return false;
    DocumentUndo entry = {.offset=offset, .removed_len=removed, .inserted_len=inserted,
        .cursor_before=document_cursor(d), .state_before=d->state_id, .state_after=d->revision + 1};
    entry.payload = malloc((usize)removed + inserted);
    if (!entry.payload) return document_fail(d, "Cannot allocate undo payload");
    document_read(d, offset, removed, entry.payload);
    // Copy aliases before materialization/reallocation, including aliases into old undo records.
    if (inserted) memcpy(entry.payload + removed, bytes, inserted);
    if (!document_apply_bytes(d, offset, removed, entry.payload + removed, inserted)) { free(entry.payload); return false; }
    entry.cursor_after = document_cursor(d);
    DocumentHistory *h = d->history;
    for (u32 i = h->position; i < h->len; ++i) free(h->items[i].payload);
    h->items[h->position++] = entry;
    h->len = h->position;
    ++d->revision;
    d->state_id = entry.state_after;
    d->last_edit = (DocumentEdit){offset, removed, {entry.payload + removed, inserted}, d->revision};
    h->error[0] = 0;
    return true;
}

internal bool document_history_move(Document *d, bool redo) {
    if (!d || !document_history_ready(d)) return false;
    DocumentHistory *h = d->history;
    if (redo ? h->position == h->len : h->position == 0) return document_fail(d, redo ? "Nothing to redo" : "Nothing to undo");
    if (d->revision == UINT64_MAX) return document_fail(d, "Document revision overflow");
    DocumentUndo *entry = &h->items[redo ? h->position : h->position - 1];
    u32 removed = redo ? entry->removed_len : entry->inserted_len;
    u32 inserted = redo ? entry->inserted_len : entry->removed_len;
    const u8 *bytes = entry->payload + (redo ? entry->removed_len : 0);
    if (!document_apply_bytes(d, entry->offset, removed, bytes, inserted)) return false;
    document_set_cursor(d, redo ? entry->cursor_after : entry->cursor_before);
    if (redo) ++h->position; else --h->position;
    ++d->revision;
    d->state_id = redo ? entry->state_after : entry->state_before;
    d->last_edit = (DocumentEdit){entry->offset, removed, {bytes, inserted}, d->revision};
    h->error[0] = 0;
    return true;
}

internal bool document_undo(Document *d) { return document_history_move(d, false); }
internal bool document_redo(Document *d) { return document_history_move(d, true); }

// A failed load leaves the previous document and history usable. The destination
// must already be initialized or zero-initialized, as for document_destroy.
internal bool document_load(Document *d, const char *path) {
    if (!d || !document_history_ready(d)) return false;
    if (!path || !*path) return document_fail(d, "Missing load path");
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return document_io_fail(d, "Open document", errno);
    struct stat st;
    if (fstat(fd, &st) != 0) { int error = errno; close(fd); return document_io_fail(d, "Stat document", error); }
    if (st.st_size < 0 || (u64)st.st_size > INT32_MAX) { close(fd); return document_fail(d, "Document exceeds INT32_MAX bytes"); }
    Document loaded = {0};
    if (!document_history_ready(&loaded)) { close(fd); return document_fail(d, "Cannot allocate loaded document"); }
    u32 cap = (u32)st.st_size, len = 0;
    if (cap) {
        loaded.text.after = malloc(cap);
        if (!loaded.text.after) { close(fd); document_destroy(&loaded); return document_fail(d, "Cannot allocate loaded text"); }
    }
    bool ok = true;
    for (;;) {
        // Probe at capacity to distinguish exact-length EOF from a growing file.
        u8 probe;
        ssize_t got = read(fd, len < cap ? loaded.text.after + len : &probe, len < cap ? cap - len : 1);
        if (got < 0) {
            if (errno == EINTR) continue;
            document_io_fail(d, "Read document", errno); ok = false; break;
        }
        if (!got) break;
        if (len == cap) {
            if (len == INT32_MAX) { document_fail(d, "Document exceeds INT32_MAX bytes"); ok = false; break; }
            u32 next = document_grown_capacity(cap, len + 1);
            u8 *bytes = realloc(loaded.text.after, next);
            if (!bytes) { document_fail(d, "Cannot grow loaded text"); ok = false; break; }
            loaded.text.after = bytes;
            cap = next;
            bytes[len++] = probe;
        } else len += (u32)got;
    }
    if (close(fd) != 0 && ok) { document_io_fail(d, "Close document", errno); ok = false; }
    if (ok && !document_utf8_valid(loaded.text.after, len)) { document_fail(d, "Document is not valid UTF-8"); ok = false; }
    if (!ok) { document_destroy(&loaded); return false; }
    document_detect_crlf(&loaded, loaded.text.after, len);
    if (len && cap != len) memmove(loaded.text.after + cap - len, loaded.text.after, len);
    loaded.text.after_cap = cap;
    loaded.text.after_len = len;
    document_destroy(d);
    *d = loaded;
    return true;
}

internal bool document_save(const Document *d, const char *path) {
    if (!d || !path || !*path) return document_fail(d, "Missing save path");
    usize len = strlen(path);
    if (len > SIZE_MAX - 12) return document_fail(d, "Save path is too long");
    char *temporary = malloc(len + 12);
    if (!temporary) return document_fail(d, "Cannot allocate save path");
    memcpy(temporary, path, len);
    memcpy(temporary + len, ".tmp.XXXXXX", 12);
    int fd = mkstemp(temporary);
    if (fd < 0) { int error = errno; free(temporary); return document_io_fail(d, "Create save file", error); }
    bool ok = true;
    int error = 0;
    const char *operation = "Write document";
    struct stat st;
    if (stat(path, &st) == 0) {
        if (fchmod(fd, st.st_mode & 0777) != 0) { ok = false; error = errno; operation = "Set save permissions"; }
    } else if (errno != ENOENT) { ok = false; error = errno; operation = "Stat save target"; }
    ByteSpan spans[3];
    u32 count = document_spans(d, 0, document_length(d), spans);
    for (u32 i = 0; ok && i < count; ++i) {
        u32 written = 0;
        while (written < spans[i].len) {
            ssize_t n = write(fd, spans[i].data + written, spans[i].len - written);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { ok = false; error = n ? errno : EIO; break; }
            written += (u32)n;
        }
    }
    if (ok && fsync(fd) != 0) { ok = false; error = errno; operation = "Sync document"; }
    if (close(fd) != 0 && ok) { ok = false; error = errno; operation = "Close save file"; }
    if (ok && rename(temporary, path) != 0) { ok = false; error = errno; operation = "Replace document"; }
    if (!ok) unlink(temporary);
    free(temporary);
    if (!ok) return document_io_fail(d, operation, error);
    if (d->history) d->history->error[0] = 0;
    return true;
}
