#pragma once

#include <lib/grim/bp.h>

// TODO create a function to return the scan table/array of bytes
// for these characters for simd scan bytes
internal force_inline(u32) strutils_is_whitespace(char ch) {
    return ch == ' ' || ch == '\t';
}

internal force_inline(u32) strutils_is_digit(char ch) {
    return ch <= '9' && ch >= '0';
}

internal force_inline(u32) strutils_is_hex(char ch) {
    return strutils_is_digit(ch) || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f');
}

internal force_inline(u32) strutils_is_quote(char ch) {
    return ch == '\'' || ch == '"';
}

internal force_inline(u32) strutils_is_line_end(char ch) {
    return ch == '\n' || ch == '\r' || ch == ';';
}
