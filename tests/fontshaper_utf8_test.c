/* clang -std=c11 -mavx2 -fsanitize=address,undefined -I src tests/fontshaper_utf8_test.c -o /tmp/utf8-test
 * Repeat without -mavx2 for the scalar build. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <lib/fontshaper/fontshaper_utf8.h>

int main(void)
{
    const struct {
        const char *sequence;
        uint32_t width, codepoint;
    } valid[] = {
        {"\0", 1, 0}, {"A", 1, 0x41}, {"\xc2\x80", 2, 0x80}, {"\xdf\xbf", 2, 0x7ff},
        {"\xe0\xa0\x80", 3, 0x800}, {"\xed\x9f\xbf", 3, 0xd7ff},
        {"\xee\x80\x80", 3, 0xe000}, {"\xf0\x90\x80\x80", 4, 0x10000},
        {"\xf4\x8f\xbf\xbf", 4, 0x10ffff},
    };
    const char *invalid[] = {
        "\x80", "\xc0\x80", "\xc1\xbf", "\xc2", "\xc2\x41", "\xe0\x9f\xbf", "\xed\xa0\x80",
        "\xf0\x8f\xbf\xbf", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xff",
    };
    for (size_t v = 0; v < sizeof(valid) / sizeof(*valid); v += 1) {
        char text[128];
        uint32_t width = valid[v].width;
        for (uint32_t i = 0; i < 32; i += 1)
            memcpy(text + i * width, valid[v].sequence, width);
        for (uint32_t cap = 1; cap <= 32; cap += 1) {
            uint32_t *points = malloc(cap * sizeof(*points));
            uint32_t *offsets = malloc(cap * sizeof(*offsets));
            assert(points != NULL && offsets != NULL);
            FontShaperUtf8Result r = font_shaper_decode_utf8_buffer(text, 32 * width, points, offsets, cap);
            assert(r.error == FONTSHAPER_UTF8_OK && r.count == cap && r.bytes_read == cap * width);
            for (uint32_t i = 0; i < cap; i += 1)
                assert(points[i] == valid[v].codepoint && offsets[i] == i * width);
            free(points);
            free(offsets);
        }
    }
    for (size_t v = 0; v < sizeof(invalid) / sizeof(*invalid); v += 1) {
        for (uint32_t at = 0; at <= 32; at += 1) {
            uint32_t bad_len = (uint32_t)strlen(invalid[v]);
            char *text = malloc(at + bad_len);
            uint32_t points[33], offsets[33];
            assert(text != NULL);
            memset(text, 'a', at);
            memcpy(text + at, invalid[v], bad_len);
            FontShaperUtf8Result r = font_shaper_decode_utf8_buffer(text, at + bad_len, points, offsets, 33);
            assert(r.error != FONTSHAPER_UTF8_OK && r.count == at && r.bytes_read == at);
            free(text);
        }
    }
    const char mixed[] = "a\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80\0";
    uint32_t points[5], offsets[5];
    FontShaperUtf8Result r = font_shaper_decode_utf8_buffer(mixed, sizeof(mixed) - 1, points, offsets, 5);
    assert(r.error == FONTSHAPER_UTF8_OK && r.count == 5 && r.bytes_read == 11);
    assert(memcmp(points, (uint32_t[]){0x61, 0xe9, 0x4e2d, 0x1f600, 0}, sizeof(points)) == 0);
    assert(memcmp(offsets, (uint32_t[]){0, 1, 3, 6, 10}, sizeof(offsets)) == 0);
}
