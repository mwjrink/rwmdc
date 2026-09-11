#pragma once

#include <lib/grim/bp.h>
#include <lib/grim/mem/arena.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum ConfigCursorShape { CONFIG_CURSOR_LINE, CONFIG_CURSOR_BLOCK } ConfigCursorShape;
typedef enum ConfigFindBinding { CONFIG_FIND_CTRLF, CONFIG_FIND_SLASH } ConfigFindBinding;

typedef struct AppConfig {
    u32 image_cache_enabled : 1;
    u32 image_cache_gpu : 1;
    u32 cursor_shape : 1;
    u32 find_binding : 1;
    u32 font_ligatures : 1;
    u32 prealloc_before, prealloc_after, prealloc_undo;
    f32 font_size;
    u32 bg, font_fg, comment_fg, block_comment_bg; /* R | G<<8 | B<<16 | A<<24. */
} AppConfig;

/* Called once for the last font.path, after all scalar values validate.
 * Both pointers expire on return; copy scalars if needed.
 * Loaded resources must live outside the configuration scratch arena. */
typedef bool (*ConfigFontConsumer)(void *userdata, const char *path, const AppConfig *values);

internal AppConfig config_defaults(void) {
    return (AppConfig){.cursor_shape=CONFIG_CURSOR_LINE, .find_binding=CONFIG_FIND_CTRLF,
        .prealloc_before=32000000, .prealloc_after=32000000, .prealloc_undo=32000000,
        .font_size=14,
        .bg=0xff140000u, .font_fg=0xffffffffu, .comment_fg=0xff888888u, .block_comment_bg=0xff26221fu};
}

typedef struct ConfigScan {
    char *at, *end, *line_begin;
    const char *path;
    usize line;
} ConfigScan;

typedef struct ConfigPending {
    AppConfig values;
    const char *font_path; /* Load-time only; never enters AppConfig. */
    ConfigScan font_site;
} ConfigPending;

internal bool config_fail(const ConfigScan *s, const char *message) {
    fprintf(stderr, "%s:%zu:%zu: %s\n", s->path, s->line, (usize)(s->at - s->line_begin) + 1, message);
    return false;
}
internal bool config_space(char c) { return c == ' ' || c == '\t'; }
internal bool config_lower(char c) { return c >= 'a' && c <= 'z'; }
internal bool config_digit(char c) { return c >= '0' && c <= '9'; }
internal void config_skip_space(ConfigScan *s) {
    while (s->at < s->end && config_space(*s->at)) ++s->at;
}
internal bool config_expect(ConfigScan *s, char c, const char *message) {
    if (s->at == s->end || *s->at != c) return config_fail(s, message);
    ++s->at;
    return true;
}
internal bool config_finish(ConfigScan *s) {
    if (s->at == s->end) return true;
    if (!config_space(*s->at)) return config_fail(s, "expected whitespace or end of value");
    config_skip_space(s);
    return s->at == s->end || *s->at == '#' || config_fail(s, "unexpected text after value");
}
#define CONFIG_IS(start, length, literal) ((length) == sizeof(literal) - 1 && !memcmp((start), (literal), sizeof(literal) - 1))

internal bool config_boolean(ConfigScan *s, bool *value) {
    char *start = s->at;
    while (s->at < s->end && config_lower(*s->at)) ++s->at;
    usize len = (usize)(s->at - start);
    if (CONFIG_IS(start,len,"true") || CONFIG_IS(start,len,"yes") || CONFIG_IS(start,len,"y")) *value = true;
    else if (CONFIG_IS(start,len,"false") || CONFIG_IS(start,len,"no") || CONFIG_IS(start,len,"n")) *value = false;
    else return config_fail(s, "expected true/yes/y or false/no/n");
    return config_finish(s);
}
internal bool config_enum(ConfigScan *s, const char *first, const char *second, u32 *value) {
    char *start = s->at;
    while (s->at < s->end && config_lower(*s->at)) ++s->at;
    usize len = (usize)(s->at - start);
    if (len == strlen(first) && !memcmp(start,first,len)) *value = 0;
    else if (len == strlen(second) && !memcmp(start,second,len)) *value = 1;
    else return config_fail(s, "invalid enumeration value");
    return config_finish(s);
}
internal bool config_size(ConfigScan *s, u32 *value) {
    if (s->at == s->end || !config_digit(*s->at)) return config_fail(s, "expected an unsigned byte size");
    u32 number = 0;
    do {
        u32 digit = (u32)(*s->at - '0');
        if (number > (UINT32_MAX - digit) / 10) return config_fail(s, "byte size exceeds u32");
        number = number * 10 + digit;
        ++s->at;
    } while (s->at < s->end && config_digit(*s->at));
    char *unit = s->at;
    while (unit < s->end && config_space(*unit)) ++unit;
    u32 scale = 1;
    if (unit < s->end && *unit >= 'A' && *unit <= 'Z') {
        s->at = unit;
        while (s->at < s->end && *s->at >= 'A' && *s->at <= 'Z') ++s->at;
        usize len = (usize)(s->at - unit);
        if (CONFIG_IS(unit,len,"B")) scale = 1;
        else if (CONFIG_IS(unit,len,"KB")) scale = 1000;
        else if (CONFIG_IS(unit,len,"MB")) scale = 1000000;
        else if (CONFIG_IS(unit,len,"GB")) scale = 1000000000;
        else return config_fail(s, "expected B, KB, MB or GB (decimal units)");
    }
    if (number > UINT32_MAX / scale) return config_fail(s, "byte size exceeds u32");
    *value = number * scale;
    return config_finish(s);
}
/* Locale-independent decimal syntax; no signs, exponent, inf or nan. */
internal bool config_number(ConfigScan *s, f64 *value, bool *decimal) {
    f64 number = 0;
    bool digits = false;
    while (s->at < s->end && config_digit(*s->at)) {
        digits = true;
        number = number * 10 + (*s->at++ - '0');
        if (!isfinite(number)) return config_fail(s, "numeric value overflow");
    }
    *decimal = s->at < s->end && *s->at == '.';
    if (*decimal) {
        ++s->at;
        if (s->at == s->end || !config_digit(*s->at)) return config_fail(s, "expected digits after decimal point");
        f64 place = .1;
        while (s->at < s->end && config_digit(*s->at)) {
            digits = true;
            number += (*s->at++ - '0') * place;
            place *= .1;
        }
    }
    if (!digits) return config_fail(s, "expected a decimal number");
    *value = number;
    return true;
}
internal bool config_font_size(ConfigScan *s, f32 *value) {
    f64 number; bool decimal;
    if (!config_number(s,&number,&decimal)) return false;
    if (number <= 0 || number > FLT_MAX || (f32)number == 0)
        return config_fail(s, "font size must be positive and representable as f32");
    *value = (f32)number;
    return config_finish(s);
}
internal int config_hex(char c) {
    if (config_digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
internal bool config_color(ConfigScan *s, u32 *value) {
    u32 rgba[4] = {0,0,0,255};
    if (*s->at == '#') {
        ++s->at;
        u32 hex = 0, count = 0;
        while (s->at < s->end && config_hex(*s->at) >= 0) {
            if (count == 8) return config_fail(s, "expected six or eight hex digits");
            hex = (hex << 4) | (u32)config_hex(*s->at++); ++count;
        }
        if (count != 6 && count != 8) return config_fail(s, "expected #RRGGBB or #RRGGBBAA");
        if (count == 8) { rgba[3] = hex & 255; hex >>= 8; }
        rgba[0] = (hex >> 16) & 255; rgba[1] = (hex >> 8) & 255; rgba[2] = hex & 255;
    } else {
        f64 components[4];
        u32 count = 0;
        bool normalized = false;
        int separator = 0; /* 1: commas, 2: whitespace. */
        for (;;) {
            bool decimal;
            if (!config_number(s,&components[count],&decimal)) return false;
            normalized |= decimal;
            ++count;
            char *end = s->at;
            config_skip_space(s);
            if (s->at == s->end || (*s->at == '#' && s->at != end)) { s->at = end; break; }
            if (count == 4) return config_fail(s, "expected three or four color components");
            int next_separator = *s->at == ',' ? 1 : s->at != end ? 2 : 0;
            if (!next_separator || (separator && separator != next_separator))
                return config_fail(s, "use commas or whitespace consistently between color components");
            separator = next_separator;
            if (separator == 1) { ++s->at; config_skip_space(s); }
        }
        if (count != 3 && count != 4) return config_fail(s, "expected three or four color components");
        for (u32 i = 0; i < count; ++i) {
            if (components[i] > (normalized ? 1.0 : 255.0))
                return config_fail(s, normalized ? "decimal color components must be in 0..1" : "integer color components must be in 0..255");
            rgba[i] = (u32)(components[i] * (normalized ? 255.0 : 1.0) + .5);
        }
    }
    *value = rgba[0] | (rgba[1] << 8) | (rgba[2] << 16) | (rgba[3] << 24);
    return config_finish(s);
}
internal bool config_path(ConfigScan *s, const char **value) {
    char *start = s->at, *end;
    if (*start == '\'' || *start == '"') {
        char quote = *start++;
        s->at = start;
        while (s->at < s->end && *s->at != quote) ++s->at;
        if (s->at == s->end) return config_fail(s, "unterminated quoted path");
        end = s->at++;
        if (!config_finish(s)) return false;
    } else {
        end = s->end;
        for (char *at = start; at < end; ++at)
            if (*at == '#' && (at == start || config_space(at[-1]))) { end = at; break; }
        while (end > start && config_space(end[-1])) --end;
    }
    if (end == start) return config_fail(s, "path must not be empty");
    *end = 0;
    *value = start;
    return true;
}
internal bool config_setting(ConfigScan *s, ConfigPending *pending) {
    AppConfig *values = &pending->values;
    char *group = s->at;
    while (s->at < s->end && (config_lower(*s->at) || *s->at == '_')) ++s->at;
    usize group_len = (usize)(s->at - group);
    if (!config_expect(s,'.',"expected '.' after configuration group")) return false;
    char *key = s->at;
    while (s->at < s->end && (config_lower(*s->at) || *s->at == '_')) ++s->at;
    usize key_len = (usize)(s->at - key);
    if (!key_len) return config_fail(s, "expected a configuration key");
    if (s->at == s->end || !config_space(*s->at)) return config_fail(s, "expected whitespace before '='");
    config_skip_space(s);
    if (!config_expect(s,'=',"expected '=' after configuration key")) return false;
    if (s->at == s->end || !config_space(*s->at)) return config_fail(s, "expected whitespace after '='");
    config_skip_space(s);
    if (s->at == s->end) return config_fail(s, "missing configuration value");
    bool boolean; u32 enumeration;
    switch (*group) {
        case 'i':
            if (!CONFIG_IS(group,group_len,"image_cache")) break;
            if (CONFIG_IS(key,key_len,"enabled")) { if (!config_boolean(s,&boolean)) return false; values->image_cache_enabled = boolean; return true; }
            if (CONFIG_IS(key,key_len,"gpu")) { if (!config_boolean(s,&boolean)) return false; values->image_cache_gpu = boolean; return true; }
            goto unknown_key;
        case 'c':
            if (CONFIG_IS(group,group_len,"cursor")) {
                if (!CONFIG_IS(key,key_len,"shape")) goto unknown_key;
                if (!config_enum(s,"line","block",&enumeration)) return false;
                values->cursor_shape = enumeration; return true;
            }
            if (!CONFIG_IS(group,group_len,"colors")) break;
            if (CONFIG_IS(key,key_len,"bg")) return config_color(s,&values->bg);
            if (CONFIG_IS(key,key_len,"font_fg")) return config_color(s,&values->font_fg);
            if (CONFIG_IS(key,key_len,"comment_fg")) return config_color(s,&values->comment_fg);
            if (CONFIG_IS(key,key_len,"block_comment_bg")) return config_color(s,&values->block_comment_bg);
            goto unknown_key;
        case 'b':
            if (CONFIG_IS(group,group_len,"binds")) {
                if (!CONFIG_IS(key,key_len,"find")) goto unknown_key;
                if (!config_enum(s,"ctrlf","slash",&enumeration)) return false;
                values->find_binding = enumeration; return true;
            }
            if (!CONFIG_IS(group,group_len,"buffer")) break;
            if (CONFIG_IS(key,key_len,"prealloc_before")) return config_size(s,&values->prealloc_before);
            if (CONFIG_IS(key,key_len,"prealloc_after")) return config_size(s,&values->prealloc_after);
            if (CONFIG_IS(key,key_len,"prealloc_undo")) return config_size(s,&values->prealloc_undo);
            goto unknown_key;
        case 'f':
            if (!CONFIG_IS(group,group_len,"font")) break;
            if (CONFIG_IS(key,key_len,"path")) {
                pending->font_site = *s;
                return config_path(s,&pending->font_path);
            }
            if (CONFIG_IS(key,key_len,"size")) return config_font_size(s,&values->font_size);
            if (CONFIG_IS(key,key_len,"ligatures")) { if (!config_boolean(s,&boolean)) return false; values->font_ligatures = boolean; return true; }
            goto unknown_key;
    }
    return config_fail(s, "unknown configuration group");
unknown_key:
    return config_fail(s, "unknown configuration key");
}
#undef CONFIG_IS

/* No allocations. source[length] must be writable. Paths are consumed before
 * returning; output contains no pointers. Parse from defaults, last assignment
 * wins. Errors leave *values unchanged, but source may have been modified.
 * No consumer runs until the whole file has passed syntax/value validation. */
internal bool config_parse(AppConfig *values, char *source, usize length, const char *path,
    ConfigFontConsumer consume_font, void *userdata) {
    ConfigPending parsed = {.values=config_defaults()};
    if (!path) path = "<config>";
    if (!source && length) { fprintf(stderr,"%s: missing configuration storage\n",path); return false; }
    if (!length) { *values = parsed.values; return true; }
    source[length] = 0;
    char *at = source, *limit = source + length;
    usize line = 1;
    while (at < limit) {
        char *end = memchr(at,'\n',(usize)(limit - at));
        char *next = end ? end + 1 : limit;
        if (!end) end = limit;
        if (end > at && end[-1] == '\r') --end;
        if (config_lower(*at)) {
            ConfigScan scan = {.at=at,.end=end,.line_begin=at,.path=path,.line=line};
            char *nul = memchr(at,0,(usize)(end-at));
            if (nul) { scan.at=nul; return config_fail(&scan,"NUL byte in configuration line"); }
            if (!config_setting(&scan,&parsed)) return false;
        }
        at = next; ++line;
    }
    if (parsed.font_path) {
        if (!consume_font) return config_fail(&parsed.font_site,"font.path requires a load-time consumer");
        if (!consume_font(userdata,parsed.font_path,&parsed.values))
            return config_fail(&parsed.font_site,"font.path consumer failed");
    }
    *values = parsed.values;
    return true;
}

/* One temporary arena allocation, no per-setting allocation. Rewind scratch on
 * every exit; preserve incoming allocations/checkpoints and retain its peak.
 * Consumer failure must release any resources that consumer partially loaded. */
internal bool config_load(AppConfig *values, Arena *scratch, const char *path,
    ConfigFontConsumer consume_font, void *userdata) {
    if (!path || !*path) { fprintf(stderr,"<config>: missing configuration path\n"); return false; }
    if (!scratch || !scratch->data || scratch->len > ARENA_CAPACITY) {
        fprintf(stderr,"%s: invalid configuration scratch arena\n",path); return false;
    }
    int fd = open(path,O_RDONLY | O_CLOEXEC);
    if (fd < 0) { fprintf(stderr,"%s: open: %s\n",path,strerror(errno)); return false; }
    struct stat st;
    if (fstat(fd,&st) != 0) { int error=errno; close(fd); fprintf(stderr,"%s: stat: %s\n",path,strerror(error)); return false; }
    if (st.st_size < 0 || (u64)st.st_size >= SIZE_MAX) { close(fd); fprintf(stderr,"%s: configuration is too large\n",path); return false; }
    usize size = (usize)st.st_size, read_size = 0;
    if (size + 1 > ARENA_CAPACITY - scratch->len) {
        close(fd); fprintf(stderr,"%s: not enough configuration arena space for %zu bytes\n",path,size+1); return false;
    }
    ArenaCheckpoint checkpoint = {.previous=scratch->ckpt,.length=scratch->len};
    char *source = arena_alloc_align(scratch,1,size + 1);
    bool ok = true;
    for (;;) {
        char probe;
        ssize_t count = read(fd,read_size < size ? source + read_size : &probe,read_size < size ? size - read_size : 1);
        if (count < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr,"%s: read: %s\n",path,strerror(errno)); ok=false; break;
        }
        if (!count) {
            if (read_size != size) { fprintf(stderr,"%s: file size changed while reading\n",path); ok=false; }
            break;
        }
        if (read_size == size) { fprintf(stderr,"%s: file size changed while reading\n",path); ok=false; break; }
        read_size += (usize)count;
    }
    if (close(fd) != 0 && ok) { fprintf(stderr,"%s: close: %s\n",path,strerror(errno)); ok=false; }
    if (ok) ok = config_parse(values,source,size,path,consume_font,userdata);
    scratch->len = checkpoint.length;
    scratch->ckpt = checkpoint.previous;
    return ok;
}
