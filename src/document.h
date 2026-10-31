#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <config.h>
#include <lib/grim/bp.h>
#include <lib/grim/math.h>
#include <lib/grim/mem/arena.h>
#include <lib/grim/mem/str.h>

typedef struct {
    u32 offset;
    u32 removed;
    u32 inserted;
    u8* data; // NULL when `inserted == 0`
} DocumentEdit;

typedef struct {
    u32 length; // total bytes
    u32 cursor; // absolute cursor offset
    u32 dirty;
} DocumentStats;

typedef struct {
    DocumentEdit* edits;
    u32           count;
    u32           cap;
    u32           index; // TODO: single cursor vs redo tail
} DocumentWal;

typedef struct {
    u8* bytes;
    u32 length;
} DocPiece;

typedef struct {
    DocPiece* pieces;
    u32       count;
    u32       total_length;
} DocView;

#define AFTER_WRITE_PTR(doc) (doc->after + doc->after_cap - doc->after_len)
#define ACTIVE_SIZE          60
// Gap buffer: before | active[60] (hot) | after. Cursor lives in active.
typedef struct {
    alignas(64) u8 active[ACTIVE_SIZE];
    u8* before;
    u8* after;

    i32 active_cursor;
    u32 active_len;

    u32 before_cap;
    u32 before_len;

    u32 after_cap;
    u32 after_len; // TODO: tail-first fill? see note below

    int fd;

    u32 dirty;
} Document;
STATIC_ASSERT(sizeof(Document) == 128);

internal void document_commit_active(rop(rw Document) doc) {
    assert(SCOPE_DOCUMENT, doc->active_cursor == (i32)doc->active_len);
    assert(SCOPE_DOCUMENT, doc->before_len + doc->active_len <= doc->before_cap);

    memory_copy(doc->before + doc->before_len, doc->active, doc->active_len);
    doc->before_len += doc->active_len;
    doc->active_len    = 0;
    doc->active_cursor = 0;

    // TODO just for debug rn
    memset(doc->active, 0, ACTIVE_SIZE);
}

internal void document_rebuild_active(rop(rw Document) doc) {
    i32 min    = -(i32)doc->before_len;
    i32 max    = (i32)(doc->active_len + doc->after_len);
    i32 cursor = clamp(min, doc->active_cursor, max);

    if (cursor >= 0 && cursor <= (i32)doc->active_len) {
        u32 tail = doc->active_len - cursor;

        assert(SCOPE_DOCUMENT, cursor <= (i32)doc->active_len);
        assert(SCOPE_DOCUMENT, doc->before_len + (u32)cursor <= doc->before_cap);
        assert(SCOPE_DOCUMENT, doc->after_len + tail <= doc->after_cap);

        memory_copy(doc->before + doc->before_len, doc->active, cursor);
        memory_copy(AFTER_WRITE_PTR(doc) - tail, doc->active + cursor, tail);

        doc->before_len += cursor;
        doc->after_len += tail;
    } else if (cursor < 0) {
        u32 back = (u32)(-cursor);

        assert(SCOPE_DOCUMENT, back <= doc->before_len);
        assert(SCOPE_DOCUMENT, doc->after_len + back + doc->active_len <= doc->after_cap);

        memory_copy(AFTER_WRITE_PTR(doc) - back - doc->active_len, doc->before + doc->before_len - back, back);
        memory_copy(AFTER_WRITE_PTR(doc) - doc->active_len, doc->active, doc->active_len);

        doc->before_len -= back;
        doc->after_len += back + doc->active_len;
    } else if (cursor > 0) {
        u32 fwd = (u32)cursor - doc->active_len;

        assert(SCOPE_DOCUMENT, fwd <= doc->after_len);
        assert(SCOPE_DOCUMENT, doc->before_len + doc->active_len + fwd <= doc->before_cap);

        memory_copy(doc->before + doc->before_len, doc->active, doc->active_len);
        memory_copy(doc->before + doc->before_len + doc->active_len, AFTER_WRITE_PTR(doc), fwd);

        doc->before_len += doc->active_len + fwd;
        doc->after_len -= fwd;
    }

    doc->active_len    = 0;
    doc->active_cursor = 0;

    // TODO just for debug rn
    memset(doc->active, 0, ACTIVE_SIZE);
}

void document_set_cursor(rop(rw Document) doc, u32 offset) {
    doc->active_cursor = (i32)offset - (i32)doc->before_len;
}

void document_move_cursor(rop(rw Document) doc, i32 delta) {
    doc->active_cursor += delta;
}

DocumentStats document_get_stats(rop(ro Document) doc) {
    return (DocumentStats){
        .length = doc->before_len + doc->active_len + doc->after_len,
        .cursor = doc->before_len + (u32)doc->active_cursor,
        .dirty  = doc->dirty,
    };
}

DocView document_get_view(rop(ro Document) doc, u32 offset, u32 len, DocPiece pieces[3]) {
    u32 total = doc->before_len + doc->active_len + doc->after_len;
    u32 start = min(offset, total);
    len       = min(len, total - start);

    // before
    if (start < doc->before_len) {
        u32 take  = min(len, doc->before_len - start);
        pieces[0] = (DocPiece){
            .bytes  = doc->before + start,
            .length = take,
        };

        start += take;
        len -= take;
    }

    // active
    if (len > 0 && start < doc->before_len + doc->active_len) {
        u32 relative_start = start - doc->before_len;
        u32 take           = min(len, doc->active_len - relative_start);

        pieces[0] = (DocPiece){
            .bytes  = (u8*)doc->active + relative_start,
            .length = take,
        };

        start += take;
        len -= take;
    }

    // after (content stored tail-first in the buffer)
    if (len > 0) {
        u32 relative_start = start - doc->before_len - doc->active_len;
        assert(SCOPE_DOCUMENT, relative_start < doc->after_len);
        u32 take = min(len, doc->after_len - relative_start);

        pieces[0] = (DocPiece){
            .bytes  = AFTER_WRITE_PTR(doc) + relative_start,
            .length = take,
        };
    }

    return (DocView){
        .pieces       = pieces,
        .count        = 3,
        .total_length = total,
    };
}

// returns bytes actually written
u32 document_read(rop(ro Document) doc, u32 offset, u32 len, rop(rw u8) out) {
    u32 total = doc->before_len + doc->active_len + doc->after_len;
    u32 start = min(offset, total);
    len       = min(len, total - start);

    u32 cursor = 0;

    // before
    if (start < doc->before_len) {
        u32 take = min(len, doc->before_len - start);

        memory_copy(out + cursor, doc->before + start, take);

        cursor += take;
        start += take;
        len -= take;
    }

    // active
    if (len > 0 && start < doc->before_len + doc->active_len) {
        u32 relative_start = start - doc->before_len;
        u32 take           = min(len, doc->active_len - relative_start);

        memory_copy(out + cursor, doc->active + relative_start, take);

        cursor += take;
        start += take;
        len -= take;
    }

    // after (content stored tail-first in the buffer)
    if (len > 0) {
        u32 relative_start = start - doc->before_len - doc->active_len;
        assert(SCOPE_DOCUMENT, relative_start < doc->after_len);
        u32 take = min(len, doc->after_len - relative_start);

        memory_copy(out + cursor, AFTER_WRITE_PTR(doc) + relative_start, take);

        cursor += take;
    }

    return cursor;
}

void document_insert(rop(rw Document) doc, const u8* data, u32 len) {
    if (doc->active_cursor != (i32)doc->active_len) {
        document_rebuild_active(doc);
    }

    if (len <= ACTIVE_SIZE - doc->active_len) {
        memory_copy(doc->active + doc->active_len, data, len);
        doc->active_len += len;
    } else {
        document_commit_active(doc);

        assert(SCOPE_DOCUMENT, doc->before_len + len <= doc->before_cap);

        memory_copy(doc->before + doc->before_len, data, len);
        doc->before_len += len;
    }

    doc->dirty = true;
}

void document_delete(rop(rw Document) doc, u32 len) {
    if (doc->active_cursor != (i32)doc->active_len) {
        document_rebuild_active(doc);
    }

    if (len <= doc->active_len) {
        doc->active_len -= len;
    } else {
        u32 remaining   = min(len - doc->active_len, doc->before_len);
        doc->active_len = 0;
        doc->before_len -= remaining;
    }

    doc->dirty = true;
}

Document document_load(rop(rw Arena) arena, ro AppSettings settings, Str path, u32 cursor_pos) {
    int fd = open(path.data, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        CRITICAL_LOG(SCOPE_LOAD, "open %.*s: %s", str_fmt(path), strerror(errno));
        exit(1);
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 0 || (u64)st.st_size > u32_MAX) {
        CRITICAL_LOG(SCOPE_LOAD, "stat %.*s", str_fmt(path));
        close(fd);
        exit(1);
    }

    u32 size   = (u32)st.st_size;
    cursor_pos = min(cursor_pos, size);

    Document doc = {.fd = fd};

    if (size == 0) {
        doc.before_cap = settings.buffer.prealloc_before;
        doc.after_cap  = settings.buffer.prealloc_after;
        doc.before     = arena_alloc_aligned(arena, char, doc.before_cap);
        doc.after      = arena_alloc_aligned(arena, char, doc.after_cap);
        return doc;
    }

    {
        const u8* mapped = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapped == MAP_FAILED) {
            CRITICAL_LOG(SCOPE_LOAD, "mmap %.*s: %s", str_fmt(path), strerror(errno));
            exit(1);
        }

        doc.before_len = cursor_pos;
        doc.before_cap = max(settings.buffer.prealloc_before, doc.before_len);
        doc.before     = arena_alloc_aligned(arena, char, doc.before_cap);
        memory_copy(doc.before, mapped, doc.before_len);

        doc.after_len = size - cursor_pos;
        doc.after_cap = max(settings.buffer.prealloc_after, doc.after_len);
        doc.after     = arena_alloc_aligned(arena, char, doc.after_cap);
        memory_copy(doc.after + doc.after_cap - doc.after_len, mapped + cursor_pos, doc.after_len);

        munmap((void*)mapped, size);
    };

    return doc;
}

void document_close(rop(rw Document) doc) {
    if (doc->fd >= 0) {
        close(doc->fd);
        doc->fd = -1;
    }
}
