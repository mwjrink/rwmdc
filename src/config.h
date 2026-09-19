#pragma once

#include <fcntl.h>
#include <float.h>
#include <lib/grim/bp.h>
#include <lib/grim/logger.h>
#include <lib/grim/mem/arena.h>
#include <math.h>
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

// Markdown colors, packed R | G<<8 | B<<16 | A<<24.
typedef struct {
    u32 bg;
    u32 font_fg;
    u32 comment_fg;
    u32 code_block_bg;
    u32 code_span_bg;
    u32 link_fg;
    u32 quote_fg;
    u32 hr_fg;
    u32 selection_bg;
    u32 cursor;
} Theme;

typedef struct {
    u32   image_cache_enabled : 1;
    u32   image_cache_gpu     : 1;
    u32   cursor_shape        : 1;
    u32   find_binding        : 1;
    u32   font_ligatures      : 1;
    u32   prealloc_before;
    u32   prealloc_after;
    u32   prealloc_undo;
    f32   font_size;
    Theme theme;
} AppConfig;

// Called once for the last font.path after all scalar values validate. Both
// pointers expire on return: copy scalars, never store the path.
typedef u32 (*ConfigFontConsumer)(void* userdata, const char* path, const AppConfig* values);

typedef struct {
    char*       at;
    char*       end;
    char*       line_begin;
    const char* path;
    usize       line;
} ConfigScan;

internal AppConfig config_defaults(void) {
    return (AppConfig){
        .cursor_shape    = ConfigCursorShape_Line,
        .find_binding    = ConfigFindBinding_CtrlF,
        .font_size       = 14,
        .prealloc_before = MB(32),
        .prealloc_after  = MB(32),
        .theme =
            {
                .bg            = 0xff140000u,
                .font_fg       = 0xffffffffu,
                .comment_fg    = 0xff888888u,
                .code_block_bg = 0xff26221fu,
                .code_span_bg  = 0xff302b27u,
                .link_fg       = 0xffffc080u,
                .quote_fg      = 0xff777777u,
                .hr_fg         = 0xff888888u,
                .selection_bg  = 0x805a87c8u,
                .cursor        = 0xffffffffu,
            },
    };
}

// ---- character/scan helpers --------------------------------------------------

internal u32 config_digit(char c) {
    return c >= '0' && c <= '9';
}
internal u32 config_lower(char c) {
    return c >= 'a' && c <= 'z';
}
internal u32 config_space(char c) {
    return c == ' ' || c == '\t';
}
internal i32 config_hex(char c) {
    if (config_digit(c))
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

internal u32 config_is(const char* start, usize len, const char* literal) {
    return len == strlen(literal) && !memcmp(start, literal, len);
}

internal u32 config_fail(const ConfigScan* s, const char* message) {
    CRITICAL_LOG(SCOPE_CONFIG, "%s:%zu:%zu: %s", s->path, s->line, (usize)(s->at - s->line_begin) + 1, message);
    return 0;
}

internal void config_skip_space(ConfigScan* s) {
    while (s->at < s->end && config_space(*s->at))
        ++s->at;
}

// Value is valid only if followed by whitespace then end or a comment.
internal u32 config_finish(ConfigScan* s) {
    if (s->at == s->end)
        return 1;
    if (!config_space(*s->at))
        return config_fail(s, "expected whitespace or end of value");
    config_skip_space(s);
    if (s->at != s->end && *s->at != '#')
        return config_fail(s, "unexpected text after value");
    return 1;
}

internal u32 config_boolean(ConfigScan* s, u32* value) {
    char* start = s->at;
    while (s->at < s->end && config_lower(*s->at))
        ++s->at;
    usize len = (usize)(s->at - start);
    if (config_is(start, len, "true") || config_is(start, len, "yes") || config_is(start, len, "y"))
        *value = 1;
    else if (config_is(start, len, "false") || config_is(start, len, "no") || config_is(start, len, "n"))
        *value = 0;
    else
        return config_fail(s, "expected true/false/yes/no/y/n");
    return config_finish(s);
}

internal u32 config_enum(ConfigScan* s, const char* a, const char* b, u32* value) {
    char* start = s->at;
    while (s->at < s->end && config_lower(*s->at))
        ++s->at;
    usize len = (usize)(s->at - start);
    if (config_is(start, len, a))
        *value = 0;
    else if (config_is(start, len, b))
        *value = 1;
    else
        return config_fail(s, "invalid enumeration value");
    return config_finish(s);
}

// Unsigned size with optional decimal unit suffix (B/KB/MB/GB).
internal u32 config_size(ConfigScan* s, u32* value) {
    if (s->at == s->end || !config_digit(*s->at))
        return config_fail(s, "expected an unsigned byte size");

    u32 number = 0;
    while (s->at < s->end && config_digit(*s->at)) {
        u32 digit = (u32)(*s->at - '0');
        if (number > (u32_MAX - digit) / 10)
            return config_fail(s, "byte size exceeds u32");
        number = number * 10 + digit;
        ++s->at;
    }

    char* unit = s->at;
    while (unit < s->end && config_space(*unit))
        ++unit;
    u32 scale = 1;
    if (unit < s->end && *unit >= 'A' && *unit <= 'Z') {
        s->at = unit;
        while (s->at < s->end && *s->at >= 'A' && *s->at <= 'Z')
            ++s->at;
        usize len = (usize)(s->at - unit);
        if (config_is(unit, len, "B"))
            scale = 1;
        else if (config_is(unit, len, "KB"))
            scale = 1000;
        else if (config_is(unit, len, "MB"))
            scale = 1000000;
        else if (config_is(unit, len, "GB"))
            scale = 1000000000;
        else
            return config_fail(s, "expected B, KB, MB or GB");
    }
    if (number > u32_MAX / scale)
        return config_fail(s, "byte size exceeds u32");
    *value = number * scale;
    return config_finish(s);
}

// Locale-independent decimal; no sign, exponent, inf or nan.
internal u32 config_number(ConfigScan* s, f64* value, u32* decimal) {
    f64 number = 0;
    u32 digits = 0;
    while (s->at < s->end && config_digit(*s->at)) {
        digits = 1;
        number = number * 10 + (*s->at++ - '0');
        if (!isfinite(number))
            return config_fail(s, "numeric value overflow");
    }
    *decimal = (s->at < s->end && *s->at == '.');
    if (*decimal) {
        ++s->at;
        if (s->at == s->end || !config_digit(*s->at))
            return config_fail(s, "expected digits after decimal point");
        f64 place = 0.1;
        while (s->at < s->end && config_digit(*s->at)) {
            digits = 1;
            number += (*s->at++ - '0') * place;
            place *= 0.1;
        }
    }
    if (!digits)
        return config_fail(s, "expected a decimal number");
    *value = number;
    return 1;
}

internal u32 config_font_size(ConfigScan* s, f32* value) {
    f64 number;
    u32 decimal;
    if (!config_number(s, &number, &decimal))
        return 0;
    if (number <= 0 || number > FLT_MAX || (f32)number == 0)
        return config_fail(s, "font size must be positive and representable as f32");
    *value = (f32)number;
    return config_finish(s);
}

// #RRGGBB / #RRGGBBAA, or 3-4 decimal components separated by commas or
// whitespace (decimal in 0..1, integer in 0..255).
internal u32 config_color(ConfigScan* s, u32* value) {
    u32 rgba[4] = {0, 0, 0, 255};
    if (*s->at == '#') {
        ++s->at;
        u32 hex = 0, count = 0;
        while (s->at < s->end && config_hex(*s->at) >= 0) {
            if (count == 8)
                return config_fail(s, "expected six or eight hex digits");
            hex = (hex << 4) | (u32)config_hex(*s->at++);
            ++count;
        }
        if (count != 6 && count != 8)
            return config_fail(s, "expected #RRGGBB or #RRGGBBAA");
        if (count == 8) {
            rgba[3] = hex & 255;
            hex >>= 8;
        }
        rgba[0] = (hex >> 16) & 255;
        rgba[1] = (hex >> 8) & 255;
        rgba[2] = hex & 255;
    } else {
        f64 components[4];
        u32 count      = 0;
        u32 normalized = 0;
        u32 separator  = 0; // 1: commas, 2: whitespace
        for (;;) {
            u32 decimal;
            if (!config_number(s, &components[count], &decimal))
                return 0;
            normalized |= decimal;
            ++count;
            char* end = s->at;
            config_skip_space(s);
            if (s->at == s->end || (*s->at == '#' && s->at != end)) {
                s->at = end;
                break;
            }
            if (count == 4)
                return config_fail(s, "expected three or four color components");
            u32 next_separator = *s->at == ',' ? 1 : (s->at != end ? 2 : 0);
            if (!next_separator || (separator && separator != next_separator))
                return config_fail(s, "use commas or whitespace consistently between color components");
            separator = next_separator;
            if (separator == 1) {
                ++s->at;
                config_skip_space(s);
            }
        }
        if (count != 3 && count != 4)
            return config_fail(s, "expected three or four color components");
        for (u32 i = 0; i < count; ++i) {
            if (components[i] > (normalized ? 1.0 : 255.0))
                return config_fail(
                    s, normalized ? "decimal components must be in 0..1" : "integer components must be in 0..255");
            rgba[i] = (u32)(components[i] * (normalized ? 255.0 : 1.0) + 0.5);
        }
    }
    *value = rgba[0] | (rgba[1] << 8) | (rgba[2] << 16) | (rgba[3] << 24);
    return config_finish(s);
}

// Quoted (with optional trailing comment) or bare until a trailing comment.
internal u32 config_path(ConfigScan* s, const char** value) {
    char* start = s->at;
    char* end;
    if (*start == '\'' || *start == '"') {
        char quote = *start++;
        s->at      = start;
        while (s->at < s->end && *s->at != quote)
            ++s->at;
        if (s->at == s->end)
            return config_fail(s, "unterminated quoted path");
        end = s->at++;
        if (!config_finish(s))
            return 0;
    } else {
        end = s->end;
        for (char* at = start; at < end; ++at) {
            if (*at == '#' && (at == start || config_space(at[-1]))) {
                end = at;
                break;
            }
        }
        while (end > start && config_space(end[-1]))
            --end;
    }
    if (end == start)
        return config_fail(s, "path must not be empty");
    *end   = 0;
    *value = start;
    return 1;
}

// ---- setting dispatch --------------------------------------------------------

internal u32 config_setting(ConfigScan* s, AppConfig* values, const char** font_path, ConfigScan* font_site) {
    char* group = s->at;
    while (s->at < s->end && (config_lower(*s->at) || *s->at == '_'))
        ++s->at;
    usize group_len = (usize)(s->at - group);
    if (s->at == s->end || *s->at != '.')
        return config_fail(s, "expected '.' after configuration group");
    ++s->at;

    char* key = s->at;
    while (s->at < s->end && (config_lower(*s->at) || *s->at == '_'))
        ++s->at;
    usize key_len = (usize)(s->at - key);
    if (!key_len)
        return config_fail(s, "expected a configuration key");

    config_skip_space(s);
    if (s->at == s->end || *s->at != '=')
        return config_fail(s, "expected '=' after configuration key");
    ++s->at;
    config_skip_space(s);
    if (s->at == s->end)
        return config_fail(s, "missing configuration value");

    u32 number;
    u32 boolean;

    if (config_is(group, group_len, "image_cache")) {
        if (config_is(key, key_len, "enabled")) {
            if (!config_boolean(s, &boolean))
                return 0;
            values->image_cache_enabled = boolean;
            return 1;
        }
        if (config_is(key, key_len, "gpu")) {
            if (!config_boolean(s, &boolean))
                return 0;
            values->image_cache_gpu = boolean;
            return 1;
        }
        return config_fail(s, "unknown configuration key");
    }

    if (config_is(group, group_len, "cursor")) {
        if (config_is(key, key_len, "shape")) {
            if (!config_enum(s, "line", "block", &number))
                return 0;
            values->cursor_shape = number;
            return 1;
        }
        return config_fail(s, "unknown configuration key");
    }

    if (config_is(group, group_len, "colors")) {
        if (config_is(key, key_len, "bg"))
            return config_color(s, &values->theme.bg);
        if (config_is(key, key_len, "font_fg"))
            return config_color(s, &values->theme.font_fg);
        if (config_is(key, key_len, "comment_fg"))
            return config_color(s, &values->theme.comment_fg);
        if (config_is(key, key_len, "code_block_bg"))
            return config_color(s, &values->theme.code_block_bg);
        if (config_is(key, key_len, "code_span_bg"))
            return config_color(s, &values->theme.code_span_bg);
        if (config_is(key, key_len, "link_fg"))
            return config_color(s, &values->theme.link_fg);
        if (config_is(key, key_len, "quote_fg"))
            return config_color(s, &values->theme.quote_fg);
        if (config_is(key, key_len, "hr_fg"))
            return config_color(s, &values->theme.hr_fg);
        if (config_is(key, key_len, "selection_bg"))
            return config_color(s, &values->theme.selection_bg);
        if (config_is(key, key_len, "cursor"))
            return config_color(s, &values->theme.cursor);
        return config_fail(s, "unknown configuration key");
    }

    if (config_is(group, group_len, "binds")) {
        if (config_is(key, key_len, "find")) {
            if (!config_enum(s, "ctrlf", "slash", &number))
                return 0;
            values->find_binding = number;
            return 1;
        }
        return config_fail(s, "unknown configuration key");
    }

    if (config_is(group, group_len, "buffer")) {
        if (config_is(key, key_len, "prealloc_before"))
            return config_size(s, &values->prealloc_before);
        if (config_is(key, key_len, "prealloc_after"))
            return config_size(s, &values->prealloc_after);
        if (config_is(key, key_len, "prealloc_undo"))
            return config_size(s, &values->prealloc_undo);
        return config_fail(s, "unknown configuration key");
    }

    if (config_is(group, group_len, "font")) {
        if (config_is(key, key_len, "path")) {
            *font_site = *s;
            return config_path(s, font_path);
        }
        if (config_is(key, key_len, "size"))
            return config_font_size(s, &values->font_size);
        if (config_is(key, key_len, "ligatures")) {
            if (!config_boolean(s, &boolean))
                return 0;
            values->font_ligatures = boolean;
            return 1;
        }
        return config_fail(s, "unknown configuration key");
    }

    return config_fail(s, "unknown configuration group");
}

internal AppConfig config_parse(Str source) {
    // For line in source
    // trim start
    // grab until .
    // compare to categories
    // grab until " "
    // compare against settings
    // grab =
    // grab value with appropriate parser
    // set value
    // repeat

    *values               = config_defaults();
    const char* font_path = NULL;
    ConfigScan  font_site = {0};

    if (!source && length) {
        ConfigScan scan = {.path = path ? path : "<config>"};
        return config_fail(&scan, "missing configuration storage");
    }
    if (!length)
        return 1;

    source[length] = 0;
    char* at       = source;
    char* limit    = source + length;
    usize line     = 1;
    while (at < limit) {
        char* end  = memchr(at, '\n', (usize)(limit - at));
        char* next = end ? end + 1 : limit;
        if (!end)
            end = limit;
        if (end > at && end[-1] == '\r')
            --end;

        if (config_lower(*at)) {
            ConfigScan scan = {.at = at, .end = end, .line_begin = at, .path = path ? path : "<config>", .line = line};
            if (memchr(at, 0, (usize)(end - at))) {
                scan.at = memchr(at, 0, (usize)(end - at));
                return config_fail(&scan, "NUL byte in configuration line");
            }
            if (!config_setting(&scan, values, &font_path, &font_site))
                return 0;
        }
        at = next;
        ++line;
    }

    if (font_path) {
    }

    return (AppConfig){
        // -
    };
}

// Loads and parses a config file. Returns 0 and logs on error.
internal AppConfig config_load(const char* path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    assert(SCOPE_CONFIG, fd >= 0);

    struct stat st;
    assert(SCOPE_CONFIG, fstat(fd, &st) == 0);
    usize size = (usize)st.st_size;

    if (size == 0) {
        close(fd);
        return config_parse((Str){0});
    }

    char* source = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd);
    assert(SCOPE_CONFIG, source != MAP_FAILED);

    Str config_data = (Str){
        .data = source,
        .len  = size,
    };

    u32 ok = config_parse(config_data);
    munmap(source, size);
    return ok;
}
