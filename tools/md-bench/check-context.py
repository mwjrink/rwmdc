#!/usr/bin/env python3
"""Check the live first-loop cursor while semantic/output bodies remain TODOs."""
from pathlib import Path
import tempfile
from compare import ROOT, run

source = (ROOT / "src/lib/md/md_parser_scratch.h").read_text()
start = source.index("MdResult md_get(")
setup = source[source.index("    u32 extra_idx", start):source.index("    // Loop 1:", start)]
a = source.index("    while (chunk_start < state->l0_count)", source.index("    // Loop 1:", start))
loop = source[a:source.index("    // AT THIS POINT:", a)]
qstart = source.index("        if (chunk_idx == buffer_size)", source.index("    // Loop 2:", start))
qend = source.index("\n        }", qstart) + len("\n        }")
query_refill = source[qstart:qend]
# Observe the cursor and metadata association before clearing the selected hit.
loop = loop.replace("        u32 l0_idx = chunk_start + chunk_idx;",
                    "        candidates += 1;\n        u32 l0_idx = chunk_start + chunk_idx;")
loop = loop.replace("        if (l0_end > begin) {", "        if (l0_end > begin) {\n            rewinds += 1;")
loop = loop.replace("                chunk_idx -= 1;", "                rewind_steps += 1;\n                chunk_idx -= 1;")
marker = "        active_hit_mask &= active_hit_mask - 1;"
assert marker in loop
loop = loop.replace(marker, r'''
        CHECK(l0_idx < expected);
        CHECK(extra_idx == extra_prefix[l0_idx] +
              (l0_tag == MD_L0_BLOCK_QUOTE || l0_tag == MD_L0_UNORDERED_LIST));
        if (active_hit_mask != 0) {
            if (l0_tag == MD_L0_BLOCK_QUOTE || l0_tag == MD_L0_UNORDERED_LIST)
                CHECK(state->l0_extra[extra_prefix[l0_idx]].post_indent == l0_idx);
            u32 wanted = 0;
            for (u32 j = 0; j < sizeof(context_tags); j += 1)
                if (l0_tag == context_tags[j]) wanted = 1;
            CHECK(wanted);
            state_seen += 1;
        }
''' + marker)
out = Path(tempfile.mkdtemp(prefix="md-check-context-"))
(out / "parser.h").write_text(source)
(out / "assert.h").write_text('''#if defined(STATIC_ASSERT)
#pragma push_macro("assert")
#include_next <assert.h>
#pragma pop_macro("assert")
#else
#include_next <assert.h>
#endif
''')
(out / "check.c").write_text(r'''
#include <lib/grim/logger.h>
#include "parser.h"
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); abort(); } } while (0)
int main(void) {
    const u8 context_tags[] = {MD_L0_FENCED_CODE_BLOCK, MD_L0_BLOCK_QUOTE,
                              MD_L0_THEMATIC_BREAK, MD_L0_UNORDERED_LIST};
    SimdByteScanMany context_table = simd_byte_scan_many_init(context_tags, sizeof(context_tags));
    ScratchArena storage = scratch_create(); ScratchArena* scratch = &storage;
    usize page = sysconf(_SC_PAGESIZE);
    const u32 sizes[] = {1, 2, 63, 64, 65, 129, 4095, 4096, 4097, 8193};
    for (u32 pattern = 0; pattern < 6; ++pattern) {
        for (u32 k = 0; k < sizeof(sizes) / sizeof(*sizes); ++k) {
            u32 n = sizes[k]; usize readable = ((n + page - 1) / page) * page;
            u8* region = mmap(NULL, readable + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            CHECK(region != MAP_FAILED && mprotect(region + readable, page, PROT_NONE) == 0);
            u8* tags = region + readable - n;
            u32* offsets = calloc(n, sizeof(u32));
            u32* extra_prefix = calloc(n, sizeof(u32));
            MdL0Extra* extras = calloc(n, sizeof(MdL0Extra));
            CHECK(offsets && extra_prefix && extras);
            u32 extra_count = 0;
            for (u32 i = 0; i < n; ++i) {
                tags[i] = MD_L0_PARAGRAPH + (i % 8);
                if (pattern == 1) tags[i] = MD_L0_PARAGRAPH;
                if (pattern == 2) tags[i] = MD_L0_BLANK;
                if (pattern == 3) tags[i] = MD_L0_UNORDERED_LIST;
                if (pattern == 4) {
                    tags[i] = MD_L0_PARAGRAPH;
                    if ((i % 2048) == 1023) tags[i] = MD_L0_FENCED_CODE_BLOCK;
                }
                if (pattern == 5) tags[i] = MD_L0_ATX_HEADING + (i % 2);
                if (i + 1 == n) tags[i] = MD_L0_EOF;
                if (i) offsets[i] = offsets[i - 1] + 1 + (i % 5);
                extra_prefix[i] = extra_count;
                if (tags[i] == MD_L0_BLOCK_QUOTE || tags[i] == MD_L0_UNORDERED_LIST) {
                    extras[extra_count].post_indent = i;
                    extra_count += 1;
                }
            }
            MdState data = {.l0_tags = tags, .l0_offsets = offsets, .l0_extra = extras, .l0_count = n};
            MdState* state = &data;
            u32 rng = 0x12345678;
            for (u32 query = 0; query < 200; ++query) {
                rng = rng * 1664525 + 1013904223;
                u32 begin = 0;
                if (offsets[n - 1] > 0) begin = rng % offsets[n - 1];
                if (query == 0) begin = 0;
                if (query == 199 && offsets[n - 1] > 0) begin = offsets[n - 1] - 1;
                if (query > 0 && query < 8 && n > 4097) begin = offsets[4093 + query];
                if (query >= 8 && query < 72 && query - 8 + 1 < n) begin = offsets[query - 8];
                u32 expected = 0, expected_states = 0;
                while (tags[expected] != MD_L0_EOF && offsets[expected + 1] <= begin) {
                    for (u32 j = 0; j < sizeof(context_tags); ++j)
                        if (tags[expected] == context_tags[j]) expected_states += 1;
                    expected += 1;
                }
                u32 state_seen = 0, candidates = 0, rewinds = 0, rewind_steps = 0, eof = 0;
                scratch->len = 0;
''' + setup + '\n                if (begin < offsets[n - 1]) {\n' + loop + r'''
                goto checked;
get_end:
                eof = 1;
checked:
                CHECK(chunk_start + chunk_idx == expected && extra_idx == extra_prefix[expected]);
                CHECK(eof == (tags[expected] == MD_L0_EOF) && state_seen == expected_states);
                CHECK(rewinds <= 1 && rewind_steps < 4096);
                CHECK(candidates <= expected_states + (n + 4095) / 4096 + 1);
                }
                if (query == 0) {
                    chunk_start = chunk_idx = buffer_size = 0;
                    for (u32 offset = 0; offset < n; offset += 4096) {
                        do {
''' + query_refill + r'''
                        } while (0);
                        CHECK(chunk_start == offset && buffer_size == min(4096, n - offset));
                        CHECK(chunk_idx == 0 && hit_mask_count == (buffer_size + 63) / 64);
                        CHECK(memcmp(buffer_ptr, tags + offset, buffer_size) == 0);
                        if (buffer_size == 4096) CHECK(buffer_ptr == tags + offset);
                        else CHECK(buffer_ptr == tail_buffer);
                        for (u32 i = 0; i < hit_mask_count * 64; ++i) {
                            CHECK(((hit_masks[i / 64] >> (i & 63)) & 1) == (i < buffer_size));
                            if (i >= buffer_size) CHECK(buffer_ptr[i] == 0);
                        }
                        chunk_idx = buffer_size;
                    }
                }
            }
            free(offsets); free(extra_prefix); free(extras);
            CHECK(munmap(region, readable + page) == 0);
        }
    }
    scratch_destroy(scratch);
    puts("PASS: live context loop; quote/list-only Extra cursor, guarded tails, mask/refill boundaries, rewind and EOF");
}
''')
common = ["-g", "-march=x86-64-v3", "-I" + str(out), "-I" + str(ROOT / "src"),
          "-DWAYLAND", str(out / "check.c"), "-lwayland-client", "-lvulkan", "-lm"]
for name, cc, flags in [("clang", "clang", ["-O3"]), ("gcc", "gcc", ["-O3"]),
                        ("asan", "clang", ["-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all"])]:
    run([cc, *flags, *common, "-o", out / name], out / (name + "-build.txt"))
    print(name + ": " + run([out / name], out / (name + "-check.txt")).strip(), flush=True)
print("Sources and logs:", out)
