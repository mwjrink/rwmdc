#pragma once

#include <fcntl.h>
#include <lib/grim/bp.h>
#include <lib/grim/logger.h>
#include <lib/grim/math.h>
#include <lib/grim/mem/arena.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum ConfigCursorShape {
    ConfigCursorShape_Line,
    ConfigCursorShape_Block,
} ConfigCursorShape;

typedef enum ConfigFindBinding {
    ConfigFindBinding_CtrlF,
    ConfigFindBinding_Slash,
} ConfigFindBinding;

// Markdown colors, packed R | G<<8 | B<<16 | A<<24. One entry per distinct
// visual treatment produced by the text layer.
typedef struct {
    // Structural
    u32 window_bg;
    u32 selection_bg;
    u32 cursor;

    // Text / blocks
    u32 font_fg; // default paragraph text
    u32 h1_fg;
    u32 h2_fg;
    u32 h3_fg;
    u32 h4_fg;
    u32 h5_fg;
    u32 h6_fg;
    u32 quote_fg;     // quote text
    u32 quote_bar_fg; // quote gutter
    u32 list_marker_fg;
    u32 code_block_fg;
    u32 code_block_bg;
    u32 html_fg;
    u32 thematic_break_fg;
    u32 comment_fg;

    // Inline spans
    u32 emphasis_fg;
    u32 strong_fg;
    u32 link_fg;
    u32 image_fg;
    u32 code_fg; // inline code text
    u32 code_span_bg;
    u32 strikethrough_fg;
    u32 insert_fg;
    u32 underline_fg;
    u32 spoiler_fg;
    u32 superscript_fg;
    u32 subscript_fg;
    u32 highlight_fg;
    u32 highlight_bg;
    u32 math_fg;
    u32 footnote_fg;

    // Tables
    u32 table_border_fg;
    u32 table_header_fg;
    u32 table_header_bg;

    // Admonitions
    u32 admonition_note_fg;
    u32 admonition_tip_fg;
    u32 admonition_important_fg;
    u32 admonition_warning_fg;
    u32 admonition_caution_fg;
} Theme;
STATIC_ASSERT(sizeof(Theme) == 168);

typedef struct {
    struct {
        u32 enabled : 1;
        u32 gpu     : 1;
    } image_cache;

    struct {
        ConfigCursorShape shape;
    } cursor;

    struct {
        ConfigFindBinding find;
    } binds;

    struct {
        u32 prealloc_before;
        u32 prealloc_after;
        u32 prealloc_undo;
    } buffer;

    struct {
        f32 size;
        u32 ligatures : 1;
    } font;
} AppSettings;
STATIC_ASSERT(sizeof(AppSettings) == 32);

typedef struct {
    Theme       theme;
    AppSettings settings;
    Str         font_path;
} ConfigParseResult;
STATIC_ASSERT(sizeof(ConfigParseResult) == 216);

typedef struct {
    char* data;
    u32   pos;
    u32   len;
} ReadCursor;
STATIC_ASSERT(sizeof(ReadCursor) == 16);

internal AppSettings config_settings_default() {
    return (AppSettings){
        .image_cache =
            {
                .enabled = 0,
                .gpu     = 0,
            },
        .cursor =
            {
                .shape = ConfigCursorShape_Line,
            },
        .binds =
            {
                .find = ConfigFindBinding_CtrlF,
            },
        .buffer =
            {
                .prealloc_before = MB(32),
                .prealloc_after  = MB(32),
                .prealloc_undo   = 0,
            },
        .font =
            {
                .size      = 14,
                .ligatures = 0,
            },
    };
}

internal Theme config_theme_default() {
    return (Theme){
        .window_bg    = 0xff140000u,
        .selection_bg = 0x805a87c8u,
        .cursor       = 0xffffffffu,

        .font_fg           = 0xffffffffu,
        .h1_fg             = 0xffffffffu,
        .h2_fg             = 0xffffffffu,
        .h3_fg             = 0xffffffffu,
        .h4_fg             = 0xffffffffu,
        .h5_fg             = 0xffffffffu,
        .h6_fg             = 0xffffffffu,
        .quote_fg          = 0xff777777u,
        .quote_bar_fg      = 0xff777777u,
        .list_marker_fg    = 0xffffffffu,
        .code_block_fg     = 0xffffffffu,
        .code_block_bg     = 0xff26221fu,
        .html_fg           = 0xff888888u,
        .thematic_break_fg = 0xff888888u,
        .comment_fg        = 0xff888888u,

        .emphasis_fg      = 0xffffffffu,
        .strong_fg        = 0xffffffffu,
        .link_fg          = 0xffffc080u,
        .image_fg         = 0xffffc080u,
        .code_fg          = 0xffffffffu,
        .code_span_bg     = 0xff302b27u,
        .strikethrough_fg = 0xff888888u,
        .insert_fg        = 0xff88ff88u,
        .underline_fg     = 0xffffffffu,
        .spoiler_fg       = 0xff888888u,
        .superscript_fg   = 0xffffffffu,
        .subscript_fg     = 0xffffffffu,
        .highlight_fg     = 0xff000000u,
        .highlight_bg     = 0xffffff00u,
        .math_fg          = 0xffffffffu,
        .footnote_fg      = 0xffffc080u,

        .table_border_fg = 0xff888888u,
        .table_header_fg = 0xffffffffu,
        .table_header_bg = 0xff26221fu,

        .admonition_note_fg      = 0xff88c0ffu,
        .admonition_tip_fg       = 0xff88ff88u,
        .admonition_important_fg = 0xffff88ffu,
        .admonition_warning_fg   = 0xffffc080u,
        .admonition_caution_fg   = 0xffff8888u,
    };
}

#define chget(cursor) cursor->data[cursor->pos]

// TODO use grim strutils instead of these
internal force_inline(u32) config_is_whitespace(char ch) {
    return ch == ' ' || ch == '\t';
}

internal force_inline(u32) config_is_digit(char ch) {
    return ch <= '9' && ch >= '0';
}

internal force_inline(u32) config_is_hex(char ch) {
    return config_is_digit(ch) || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f');
}

internal force_inline(u32) config_is_quote(char ch) {
    return ch == '\'' || ch == '"';
}

internal force_inline(u32) config_is_line_end(char ch) {
    return ch == '\n' || ch == '\r' || ch == ';';
}

internal void config_skip_whitespace(rop(rw ReadCursor) cursor) {
    while (cursor->pos < cursor->len && config_is_whitespace(chget(cursor))) {
        cursor->pos += 1;
    }
}

internal ReadCursor config_get_line(rop(rw ReadCursor) cursor) {
    u32 og_pos = cursor->pos;
    while (cursor->pos < cursor->len && !config_is_line_end(chget(cursor))) {
        cursor->pos += 1;
    }
    u32 content_end = cursor->pos;
    while (cursor->pos < cursor->len && config_is_line_end(chget(cursor))) {
        cursor->pos += 1;
    }

    return (ReadCursor){
        .data = cursor->data + og_pos,
        .len  = content_end - og_pos,
        .pos  = 0,
    };
}

internal Str config_trim_to_ws(Str value) {
    u32 new_len = 0;
    while (new_len < value.len && !config_is_whitespace(value.data[new_len])) {
        new_len += 1;
    }

    return (Str){
        .data = value.data,
        .len  = new_len,
    };
}

internal Str config_get_category(rop(rw ReadCursor) line) {
    config_skip_whitespace(line);

    u32 start = line->pos;
    while (line->pos < line->len && chget(line) != '.')
        line->pos += 1;
    assert(SCOPE_CONFIG, line->pos < line->len && chget(line) == '.');

    Str category = (Str){.data = line->data + start, .len = line->pos - start};
    category     = config_trim_to_ws(category);

    line->pos += 1; // skip '.'
    return category;
}

internal Str config_get_key(rop(rw ReadCursor) line) {
    config_skip_whitespace(line);

    u32 start = line->pos;
    while (line->pos < line->len && chget(line) != '=')
        line->pos += 1;
    assert(SCOPE_CONFIG, line->pos < line->len && chget(line) == '=');

    Str key = (Str){.data = line->data + start, .len = line->pos - start};
    key     = config_trim_to_ws(key);

    line->pos += 1; // skip '='
    return key;
}

internal Str config_get_value(rop(rw ReadCursor) cursor) {
    config_skip_whitespace(cursor);
    assert(SCOPE_CONFIG, cursor->pos < cursor->len);

    u32  start = cursor->pos;
    char quote = chget(cursor);
    if (config_is_quote(quote)) {
        cursor->pos += 1;
        start = cursor->pos;

        while (cursor->pos < cursor->len && chget(cursor) != quote)
            cursor->pos += 1;
        assert(SCOPE_CONFIG, cursor->pos < cursor->len && chget(cursor) == quote);
        cursor->pos += 1;
    } else {
        start = cursor->pos;

        while (cursor->pos < cursor->len && !config_is_line_end(chget(cursor)))
            cursor->pos += 1;
    }

    return (Str){
        .data = cursor->data + start,
        .len  = cursor->pos - start,
    };
}

internal u32 config_parse_boolean(rop(rw ReadCursor) cursor) {
    Str value = config_get_value(cursor);
    value     = config_trim_to_ws(value);

    // clang-format off
    switch (value.len) {
        case 5:
            if (str_equals(value, str_create("false"))) { return false; }
            goto fail;
        case 4: 
            if (str_equals(value, str_create("true"))) { return true; }
            goto fail;
        case 3:
            if (str_equals(value, str_create("yes"))) { return true; }
            goto fail;
        case 2:
            if (str_equals(value, str_create("no"))) { return false; }
            goto fail;
        case 1:
            if (str_equals(value, str_create("y")) || str_equals(value, str_create("1"))) { return true; }
            if (str_equals(value, str_create("n")) || str_equals(value, str_create("0"))) { return false; }
            goto fail;
        default:
            goto fail;
    }
    // clang-format on

fail:
    CRITICAL_LOG(SCOPE_CONFIG, "Invalid config value for boolean: %.*s", str_fmt(value));
    exit(1);
}

internal f32 config_parse_number(Str value) {
    f64 parsed      = 0.0;
    f64 decimal_idx = 0.0;
    for (u32 sidx = 0; sidx < value.len; sidx++) {
        char ch = value.data[sidx];

        if (config_is_digit(ch)) {
            u32 digit = ch - '0';
            if (decimal_idx > 0.0) {
                parsed += (f64)digit / decimal_idx;
                decimal_idx *= 10.0;
            } else {
                parsed *= 10;
                parsed += (f64)digit;
            }
        } else if (ch == '.') {
            assert(SCOPE_CONFIG, decimal_idx != 0.0);
            decimal_idx = 10.0;
        } else if (config_is_whitespace(ch) || ch == '_' || ch == ',') {
            continue;
        }
    }

    return (f32)parsed;
}

internal u32 config_parse_bytes(rop(rw ReadCursor) cursor) {
    Str value = config_get_value(cursor);

    u32 unit_mul = 1;
    if (str_ends_with(value, str_create("GB"))) {
        unit_mul = GB(1);
        value.len -= 2;
    } else if (str_ends_with(value, str_create("MB"))) {
        unit_mul = MB(1);
        value.len -= 2;
    } else if (str_ends_with(value, str_create("KB"))) {
        unit_mul = KB(1);
        value.len -= 2;
    } else if (str_ends_with(value, str_create("B"))) {
        unit_mul = 1;
        value.len -= 1;
    }

    f32 parsed = config_parse_number(value);

    assert(SCOPE_CONFIG, parsed >= 0 && (f64)parsed * (f64)unit_mul <= (f64)u32_MAX);
    return (u32)roundf(parsed * (f32)unit_mul);
}

internal f32 config_parse_font_size(rop(rw ReadCursor) cursor) {
    Str value = config_get_value(cursor);

    // clang-format off
    if (str_ends_with(value, str_create("px"))) { value.len -= 2; }
    // clang-format on

    f32 parsed = config_parse_number(value);
    assert(SCOPE_CONFIG, parsed > 0 && isfinite(parsed));

    return parsed;
}

// #RRGGBB / #RRGGBBAA, or 3-4 decimal components separated by commas or
// whitespace (decimal in 0..1, integer in 0..255).
internal u32 config_parse_color(rop(rw ReadCursor) cursor) {
    Str value = config_get_value(cursor);
    assert(SCOPE_CONFIG, value.len > 0);

    u32 color = 0x000000FF;
    if (value.data[0] == '#') {
        value.data += 1;
        value.len -= 1;

        assert(SCOPE_CONFIG, value.len == 6 || value.len == 8);

        for (u32 ch_idx = 0; ch_idx < value.len; ch_idx++) {
            char ch = value.data[ch_idx];
            assert(SCOPE_CONFIG, config_is_hex(ch));

            u32 offset = 28 - 4 * ch_idx;
            if (config_is_digit(ch)) {
                color |= (u32)(ch - '0') << offset;
            } else if (ch >= 'a' && ch <= 'f') {
                color |= (u32)(ch - 'a' + 10) << offset;
            } else {
                color |= (u32)(ch - 'A' + 10) << offset;
            }
        }
        return color;
    }

    // Decimal components: 3 (rgb) or 4 (rgba), separated by ',' or whitespace.
    // If any component carries a '.', every component is normalized 0..1;
    // otherwise every component is an integer byte 0..255.
    u32 normalized = 0;
    for (u32 i = 0; i < value.len; i++) {
        if (value.data[i] == '.') {
            normalized = true;
            break;
        }
    }

    u32   component_count = 0;
    char* at              = value.data;
    for (u32 i = 0; i < 4; i++) {
        while (value.len && (config_is_whitespace(*at) || *at == ',')) {
            at += 1;
            value.len -= 1;
        }
        if (!value.len)
            break;

        u32 new_len = 0;
        while (new_len < value.len && !config_is_whitespace(at[new_len]) && at[new_len] != ',')
            new_len += 1;

        Str component_str_slice = {.data = at, .len = new_len};
        at += new_len;
        value.len -= new_len;

        f32 parsed_component = config_parse_number(component_str_slice);
        u32 component;
        if (normalized) {
            assert(SCOPE_CONFIG, parsed_component >= 0.0f && parsed_component <= 1.0f);
            component = (u32)roundf(parsed_component * 255.0f);
        } else {
            assert(SCOPE_CONFIG, parsed_component >= 0.0f && parsed_component <= 255.0f);
            component = (u32)roundf(parsed_component);
        }

        color |= component << (24 - 8 * i);
        component_count += 1;
    }

    assert(SCOPE_CONFIG, component_count == 3 || component_count == 4);
    return color;
}

// Parse from defaults, last assignment wins. source[length] must be writable.
// On error `values` is untouched. font_path, if set, points into `source` and
// is only valid until `source` is freed; callers must copy it before then.
//
//     For line in source
//     trim start
//     grab until .
//     compare to categories
//     grab until " "
//     compare against settings
//     grab =
//     grab value with appropriate parser
//     set value
//     repeat
internal ConfigParseResult config_parse(rop(rw ScratchArena) scratch, Str content) {
    ReadCursor cursor = {
        .data = content.data,
        .pos  = 0,
        .len  = content.len,
    };

    ConfigParseResult config = {
        .settings  = config_settings_default(),
        .theme     = config_theme_default(),
        .font_path = {0},
    };
    while (true) {
        ReadCursor line = config_get_line(&cursor);

        // Blank/comment line, or end of content: nothing to parse.
        if (!line.len)
            break;
        config_skip_whitespace(&line);
        if (line.pos >= line.len || chget((&line)) == '#')
            continue;

        Str category = config_get_category(&line); // parse from start of line to . trim whitespace
        Str key      = config_get_key(&line);      // parse from period to = trim whitespace
        // Str value    = config_get_value(&line); // handled by parse_bool, parse_color etc

        switch (category.data[0]) {
            case 'i':
                if (str_equals(category, str_create("image_cache"))) {
                    if (str_equals(key, str_create("enabled"))) {
                        config.settings.image_cache.enabled = config_parse_boolean(&line);
                        break;
                    } else if (str_equals(key, str_create("gpu"))) {
                        config.settings.image_cache.gpu = config_parse_boolean(&line);
                        break;
                    }
                }
                goto unknown_key;
            case 'c':
                if (str_equals(category, str_create("cursor"))) {
                    if (str_equals(key, str_create("shape"))) {
                        Str value = config_get_value(&line);
                        value     = config_trim_to_ws(value);
                        if (str_equals(value, str_create("line"))) {
                            config.settings.cursor.shape = ConfigCursorShape_Line;
                        } else if (str_equals(value, str_create("block"))) {
                            config.settings.cursor.shape = ConfigCursorShape_Block;
                        } else {
                            goto unknown_key;
                        }
                        break;
                    }
                }
                goto unknown_key;
            case 'b':
                if (str_equals(category, str_create("binds"))) {
                    if (str_equals(key, str_create("find"))) {
                        Str value = config_get_value(&line);
                        value     = config_trim_to_ws(value);
                        if (str_equals(value, str_create("ctrlf"))) {
                            config.settings.binds.find = ConfigFindBinding_CtrlF;
                        } else if (str_equals(value, str_create("slash"))) {
                            config.settings.binds.find = ConfigFindBinding_Slash;
                        } else {
                            goto unknown_key;
                        }
                        break;
                    }
                } else if (str_equals(category, str_create("buffer"))) {
                    if (str_equals(key, str_create("prealloc_before"))) {
                        config.settings.buffer.prealloc_before = config_parse_bytes(&line);
                        break;
                    } else if (str_equals(key, str_create("prealloc_after"))) {
                        config.settings.buffer.prealloc_after = config_parse_bytes(&line);
                        break;
                    } else if (str_equals(key, str_create("prealloc_undo"))) {
                        config.settings.buffer.prealloc_undo = config_parse_bytes(&line);
                        break;
                    }
                }
                goto unknown_key;
            case 'f':
                if (str_equals(category, str_create("font"))) {
                    if (str_equals(key, str_create("path"))) {
                        Str path = config_get_value(&line);
                        path     = config_trim_to_ws(path);

                        config.font_path.data            = scratch_alloc(scratch, path.len + 1);
                        config.font_path.len             = path.len;
                        config.font_path.null_terminated = true;

                        memory_copy(config.font_path.data, path.data, path.len);
                        config.font_path.data[path.len] = 0;

                        break;
                    } else if (str_equals(key, str_create("size"))) {
                        config.settings.font.size = config_parse_font_size(&line);
                        break;
                    } else if (str_equals(key, str_create("ligatures"))) {
                        config.settings.font.ligatures = config_parse_boolean(&line);
                        break;
                    }
                }
                goto unknown_key;
            case 't':
                if (str_equals(category, str_create("theme"))) {
                    // Structural
                    if (str_equals(key, str_create("window_bg"))) {
                        config.theme.window_bg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("selection_bg"))) {
                        config.theme.selection_bg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("cursor"))) {
                        config.theme.cursor = config_parse_color(&line);
                    }
                    // Text / blocks
                    else if (str_equals(key, str_create("font_fg"))) {
                        config.theme.font_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("h1_fg"))) {
                        config.theme.h1_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("h2_fg"))) {
                        config.theme.h2_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("h3_fg"))) {
                        config.theme.h3_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("h4_fg"))) {
                        config.theme.h4_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("h5_fg"))) {
                        config.theme.h5_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("h6_fg"))) {
                        config.theme.h6_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("quote_fg"))) {
                        config.theme.quote_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("quote_bar_fg"))) {
                        config.theme.quote_bar_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("list_marker_fg"))) {
                        config.theme.list_marker_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("code_block_fg"))) {
                        config.theme.code_block_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("code_block_bg"))) {
                        config.theme.code_block_bg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("html_fg"))) {
                        config.theme.html_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("thematic_break_fg"))) {
                        config.theme.thematic_break_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("comment_fg"))) {
                        config.theme.comment_fg = config_parse_color(&line);
                    }
                    // Inline spans
                    else if (str_equals(key, str_create("emphasis_fg"))) {
                        config.theme.emphasis_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("strong_fg"))) {
                        config.theme.strong_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("link_fg"))) {
                        config.theme.link_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("image_fg"))) {
                        config.theme.image_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("code_fg"))) {
                        config.theme.code_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("code_span_bg"))) {
                        config.theme.code_span_bg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("strikethrough_fg"))) {
                        config.theme.strikethrough_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("insert_fg"))) {
                        config.theme.insert_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("underline_fg"))) {
                        config.theme.underline_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("spoiler_fg"))) {
                        config.theme.spoiler_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("superscript_fg"))) {
                        config.theme.superscript_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("subscript_fg"))) {
                        config.theme.subscript_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("highlight_fg"))) {
                        config.theme.highlight_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("highlight_bg"))) {
                        config.theme.highlight_bg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("math_fg"))) {
                        config.theme.math_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("footnote_fg"))) {
                        config.theme.footnote_fg = config_parse_color(&line);
                    }
                    // Tables
                    else if (str_equals(key, str_create("table_border_fg"))) {
                        config.theme.table_border_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("table_header_fg"))) {
                        config.theme.table_header_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("table_header_bg"))) {
                        config.theme.table_header_bg = config_parse_color(&line);
                    }
                    // Admonitions
                    else if (str_equals(key, str_create("admonition_note_fg"))) {
                        config.theme.admonition_note_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("admonition_tip_fg"))) {
                        config.theme.admonition_tip_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("admonition_important_fg"))) {
                        config.theme.admonition_important_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("admonition_warning_fg"))) {
                        config.theme.admonition_warning_fg = config_parse_color(&line);
                    } else if (str_equals(key, str_create("admonition_caution_fg"))) {
                        config.theme.admonition_caution_fg = config_parse_color(&line);
                    } else {
                        goto unknown_key;
                    }
                    break;
                }
                goto unknown_key;
            unknown_key:
                CRITICAL_LOG(SCOPE_CONFIG, "Unknown category: %.*s.%.*s", str_fmt(category), str_fmt(key));
                exit(1);
        }
    }

    return config;
}

internal ConfigParseResult config_load(rop(rw ScratchArena) scratch) {
    ConfigParseResult defaults = {
        .settings  = config_settings_default(),
        .theme     = config_theme_default(),
        .font_path = {0},
    };

    // Default config location: $HOME/.config/rwmd.conf
    const char* home = getenv("HOME");
    const char* path;
    if (home) {
        usize home_len = strlen(home);
        char* path_buf = scratch_alloc(scratch, home_len + sizeof("/.config/rwmd.conf"));
        memory_copy(path_buf, home, home_len);
        memory_copy(path_buf + home_len, "/.config/rwmd.conf", sizeof("/.config/rwmd.conf"));
        path = path_buf;
    } else {
        CRITICAL_LOG(SCOPE_CONFIG, "$HOME is not set");
        exit(1);
    }

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return defaults;

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return defaults;
    }
    usize size = (usize)st.st_size;
    if (size == 0)
        return defaults;

    char* source = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd);
    assert(SCOPE_CONFIG, source != MAP_FAILED);

    Str file_contents = {
        .data = source,
        .len  = (u32)size,
    };
    ConfigParseResult config = config_parse(scratch, file_contents);

    munmap(source, size);

    return config;
}

#undef chget
