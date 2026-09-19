#pragma once

#include <lib/grim/bp.h>
#include <lib/grim/mem/arena.h>
#include <stdalign.h>

typedef struct DocumentContext {

} DocumentContext;

// TODO move this out of here
typedef enum EditorState {
    INSERT = 0,
    NORMAL = 1,
    VISUAL = 2,
    FIND   = 3,
} EditorState;

typedef struct DocumentState {
    alignas(64) u8 active[60];
    u8* before;
    u8* after;

    i32 active_cursor;
    u32 active_len;

    u32 before_cap;
    u32 before_len;

    u32 after_cap;
    u32 after_len;
} DocumentState;

typedef struct DocumentEdit {
    u32 offset;
    i32 length; // negative for removed
    u8* data;   // NULL if not inserted
} DocumentEdit;

typedef struct DocumentWal {
    DocumentEdit* edits;
    u32           edits_count;
    u32           edits_cap;
} DocumentWal;

internal void _document_rebuild_edit_state(rop(rw DocumentState) state, rop(rw ScratchArena) scratch) {
    // move data into before, after & rebuild active
    //
}

void document_set_cursor(rop(rw DocumentState) state, u32 pos) {
    state->active_cursor = pos - state->before_len;
    _document_rebuild_edit_state(state);
}

void document_move_cursor(rop(rw DocumentState) state, i32 offset) {
    state->active_cursor += offset;
    _document_rebuild_edit_state(state);
}

DocumentContext document_context_create() {
}

DocumentState document_load(rop(ro DocumentContext) ctx, rop(rw Arena) arena, String path) {
}
