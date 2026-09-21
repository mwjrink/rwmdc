#pragma once

#include <document.h>
#include <lib/grim/bp.h>
#include <lib/grim/math.h>
#include <lib/grim/mem/str.h>

typedef enum EditorMode {
    INSERT = 0,
    NORMAL = 1,
    VISUAL = 2,
    FIND   = 16,
} EditorMode;

typedef struct Editor {
    Document    document;
    DocumentWal wal;

    Vec2 scroll;

    Str        path; // len 0 == untitled
    EditorMode mode;
    u32        selection_anchor;

    // TODO: background save thread + sync state
} Editor;

typedef struct EditorUiState {
    f32        cursor_percent;
    Str        path;
    EditorMode mode;
    u32        dirty;
} EditorUiState;

internal Editor editor_create(rop(rw Arena) arena, ro AppSettings settings, Str path) {
    Editor editor = {0};

    // Create owned copy of path
    if (path.len) {
        editor.path.data            = arena_alloc_aligned(arena, char, path.len + 1);
        editor.path.len             = path.len;
        editor.path.null_terminated = true;
        memory_copy(editor.path.data, path.data, path.len);
        editor.path.data[path.len] = 0;
    }

    editor.document = document_load(arena, settings, editor.path, 0);

    return editor;
}

void editor_destroy(rop(rw Editor) editor) {
    document_close(&editor->document);
}

// TODO figure out InputEvent and where to put it
typedef struct {
} InputEvent;
void editor_handle_input(rop(rw Editor) editor, InputEvent event); // TODO define InputEvent

void editor_undo(rop(rw Editor) editor);
void editor_redo(rop(rw Editor) editor);

// for rendering ui: status line ui at the bottom.
EditorUiState editor_get_ui_state(rop(rw Editor) editor);
// for rendering text
typedef struct {
} TextWindow;
TextWindow editor_get_window(rop(rw Editor) editor);
