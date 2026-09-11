#pragma once

#include <lib/grim/bp.h>
#include "parser.h"
#include <utf8proc.h>
#include <time.h>
#include "entities.h"

/* Only offsets survive a parser callback. The contiguous mirror is patched from
 * DocumentEdit; MD4C never receives a flattened copy of the document per frame. */
typedef struct MdRun {
    u32 begin, end, style, block;
    MD_TEXTTYPE type;
    u32 synthetic; /* Nonzero for MD4C's normalized, non-source text. */
    u32 repeat;
} MdRun;
typedef struct MdBlock {
    u32 begin, end, first_run, run_count, quote_depth, list_depth, number;
    u32 first_point, point_count, first_span, span_count, line_count;
    MdRange content, open, close;
    MD_BLOCKTYPE type;
    f32 scale;
    bool bullet, ordered, fenced, local_safe;
} MdBlock;
typedef struct MdIndex {
    MdRun *runs;
    MdBlock *blocks;
    MdEvent *spans; /* Owned offset-only inline delimiter records. */
    u32 span_count, span_cap;
    u32 *list_numbers;
    bool *list_ordered;
    u32 list_cap, ordered_cap;
    u32 *points; /* Sparse true grapheme boundaries, at most ~1 per KiB. */
    u32 point_count, point_cap;
    u32 run_count, run_cap, block_count, block_cap;
} MdIndex;

internal bool md_reserve(void **ptr, u32 *cap, u32 need, usize item_size) {
    if (need <= *cap) return true;
    u32 next = *cap ? *cap : 64;
    while (next < need) {
        if (next > UINT32_MAX / 2) { next = need; break; }
        next *= 2;
    }
    if ((usize)next > SIZE_MAX / item_size) return false;
    void *p = realloc(*ptr, (usize)next * item_size);
    if (!p) return false;
    *ptr = p; *cap = next;
    return true;
}
internal u64 md_clock_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (u64)t.tv_sec * 1000000000ull + (u64)t.tv_nsec;
}
internal void md_index_destroy(MdIndex *index) {
    free(index->runs); free(index->blocks); free(index->points); free(index->spans);
    free(index->list_numbers); free(index->list_ordered); *index = (MdIndex){0};
}

#define MD_STYLE_BOLD 1u
#define MD_STYLE_ITALIC 2u
#define MD_STYLE_LINK 4u
#define MD_STYLE_CODE 8u

typedef struct MdParse {
    u32 current, quotes, lists, strong, emphasis, links, code;
    bool item_pending;
    MdIndex *index;
} MdParse;

internal bool md_leaf(MD_BLOCKTYPE t) {
    return t == MD_BLOCK_P || t == MD_BLOCK_H || t == MD_BLOCK_CODE ||
        t == MD_BLOCK_HTML || t == MD_BLOCK_HR || t == MD_BLOCK_BLANK;
}
internal int md_index_event(const MdEvent *event, void *opaque) {
    MdParse *p = opaque;
    MdIndex *idx = p->index;
    int type = event->type;
    if (event->kind == MD_EVENT_BLOCK_ENTER) {
        if (type == MD_BLOCK_QUOTE) ++p->quotes;
        if (type == MD_BLOCK_UL || type == MD_BLOCK_OL) {
            if (!md_reserve((void **)&idx->list_numbers, &idx->list_cap, p->lists + 1, sizeof(u32)) ||
                !md_reserve((void **)&idx->list_ordered, &idx->ordered_cap, p->lists + 1, sizeof(bool))) return 1;
            idx->list_ordered[p->lists] = type == MD_BLOCK_OL;
            idx->list_numbers[p->lists++] = event->number;
        }
        if (type == MD_BLOCK_LI) p->item_pending = true;
        if (!md_leaf((MD_BLOCKTYPE)type)) return 0;
        if (!md_reserve((void **)&idx->blocks, &idx->block_cap, idx->block_count + 1, sizeof(MdBlock))) return 1;
        p->current = idx->block_count++;
        MdBlock *b = &idx->blocks[p->current];
        *b = (MdBlock){.begin=event->source.begin, .end=event->source.end,
            .content=event->content, .open=event->open, .close=event->close,
            .first_run=idx->run_count, .first_span=idx->span_count, .line_count=event->line_count,
            .quote_depth=p->quotes, .list_depth=p->lists, .type=(MD_BLOCKTYPE)type, .scale=1,
            .bullet=p->item_pending && p->lists, .ordered=p->lists && idx->list_ordered[p->lists - 1],
            .number=p->lists ? idx->list_numbers[p->lists - 1] : 0,
            .fenced=(event->flags & MD_EVENT_FENCED) != 0};
        if (type != MD_BLOCK_BLANK) p->item_pending = false;
        if (type == MD_BLOCK_H) {
            const f32 scales[] = {1, 1.75f, 1.5f, 1.3f, 1.15f, 1.05f, 1};
            b->scale = scales[min(event->level, 6u)];
        }
        return 0;
    }
    if (event->kind == MD_EVENT_BLOCK_LEAVE) {
        if (md_leaf((MD_BLOCKTYPE)type)) {
            if (p->current == UINT32_MAX) return 1;
            MdBlock *b = &idx->blocks[p->current];
            b->run_count = idx->run_count - b->first_run;
            b->span_count = idx->span_count - b->first_span;
            p->current = UINT32_MAX;
        }
        if (type == MD_BLOCK_QUOTE) --p->quotes;
        if (type == MD_BLOCK_LI && p->lists) ++idx->list_numbers[p->lists - 1];
        if (type == MD_BLOCK_UL || type == MD_BLOCK_OL) --p->lists;
        return 0;
    }
    if (event->kind == MD_EVENT_SPAN_ENTER || event->kind == MD_EVENT_SPAN_LEAVE) {
        if (event->kind == MD_EVENT_SPAN_ENTER) {
            if (!md_reserve((void **)&idx->spans, &idx->span_cap, idx->span_count + 1, sizeof(MdEvent))) return 1;
            idx->spans[idx->span_count++] = *event;
        }
        u32 *depth = type == MD_SPAN_STRONG ? &p->strong : type == MD_SPAN_EM ? &p->emphasis :
            type == MD_SPAN_A ? &p->links : type == MD_SPAN_CODE ? &p->code : NULL;
        if (depth) {
            if (event->kind == MD_EVENT_SPAN_ENTER) ++*depth;
            else --*depth;
        }
        return 0;
    }
    if (event->kind != MD_EVENT_TEXT) return 0;
    if (p->current == UINT32_MAX) return 1;
    if (!md_reserve((void **)&idx->runs, &idx->run_cap, idx->run_count + 1, sizeof(MdRun))) return 1;
    const MdBlock *b = &idx->blocks[p->current];
    u32 style = (p->strong || b->type == MD_BLOCK_H ? MD_STYLE_BOLD : 0) |
        (p->emphasis ? MD_STYLE_ITALIC : 0) | (p->links ? MD_STYLE_LINK : 0) |
        (p->code || b->type == MD_BLOCK_CODE ? MD_STYLE_CODE : 0);
    idx->runs[idx->run_count++] = (MdRun){event->source.begin, event->source.end, style, p->current,
        (MD_TEXTTYPE)type, event->flags & MD_EVENT_SYNTHETIC ? event->codepoint : 0, event->repeat};
    return 0;
}

internal bool md_parse_index(MdParser *parser, MdIndex *idx, const u8 *source, u32 length,
    MdParserProfile *profile, u64 *index_ns, u64 *grapheme_ns) {
    idx->run_count = idx->block_count = idx->point_count = idx->span_count = 0;
    MdParse p = {.current=UINT32_MAX, .index=idx};
    int result = md_parser_parse(parser, (const char *)source, length,
        MD_DIALECT_COMMONMARK | MD_FLAG_PRESERVEBLANKLINES, md_index_event, &p, profile);
    if (result) return false;
    u64 start = md_clock_ns();
    for (u32 i = 0; i < idx->block_count; ++i) {
        MdBlock *b = &idx->blocks[i];
        b->local_safe = b->type == MD_BLOCK_P && !b->quote_depth && !b->list_depth;
        /* Reference definitions and uses depend on document-wide context. */
        if (b->local_safe)
            for (u32 j = b->begin; j < b->end; ++j)
                if (source[j] == '[' || source[j] == ']') { b->local_safe = false; break; }
    }
    *index_ns += md_clock_ns() - start;
    start = md_clock_ns();
    bool success = true;
    for (u32 i = 0; success && i < idx->block_count; ++i) {
        MdBlock *b = &idx->blocks[i];
        b->first_point = idx->point_count;
        u32 at = b->begin, previous_point = at;
        if (!md_reserve((void **)&idx->points, &idx->point_cap, idx->point_count + 1, sizeof(u32))) { success = false; break; }
        idx->points[idx->point_count++] = at;
        /* No later checkpoint can fit: there is no Unicode context to build
         * until a seek actually enters this short block. */
        if (b->end - at <= 1024) { b->point_count = 1; continue; }
        i32 previous = 0, state = 0;
        bool first = true;
        while (at < b->end) {
            i32 cp = source[at];
            utf8proc_ssize_t n = 1;
            if (cp >= 128) n = utf8proc_iterate(source + at, b->end - at, &cp);
            if (n < 1) { n = 1; cp = 0xfffd; }
            bool boundary;
            if (previous < 128 && cp < 128) {
                boundary = first || previous != '\r' || cp != '\n';
                state = 0;
            } else boundary = first || utf8proc_grapheme_break_stateful(previous, cp, &state);
            if (boundary && at - previous_point >= 1024) {
                if (!md_reserve((void **)&idx->points, &idx->point_cap, idx->point_count + 1, sizeof(u32))) { success = false; break; }
                idx->points[idx->point_count++] = at; previous_point = at;
            }
            at += (u32)n; previous = cp; first = false;
        }
        b->point_count = idx->point_count - b->first_point;
    }
    *grapheme_ns += md_clock_ns() - start;
    return success;
}

/* Decode entities only when MD4C identified them outside code/HTML. Unknown
 * names remain literal, as required by CommonMark. */
internal u32 md_entity(const u8 *s, u32 len, i32 out[2]) {
    if (len < 3 || s[0] != '&' || s[len - 1] != ';') return 0;
    if (s[1] == '#') {
        u32 at = 2, radix = 10, cp = 0;
        if (at < len && (s[at] == 'x' || s[at] == 'X')) { radix = 16; ++at; }
        for (; at + 1 < len; ++at) {
            u32 digit = s[at] <= '9' ? s[at] - '0' : (s[at] | 32) - 'a' + 10;
            if (digit >= radix) return 0;
            cp = cp > 0x10ffff / radix ? 0x110000 : cp * radix + digit;
        }
        if (!cp || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) cp = 0xfffd;
        out[0] = (i32)cp; return 1;
    }
    u32 lo = 0, hi = (u32)(sizeof(md_entities) / sizeof(md_entities[0]));
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        const MdEntity *e = &md_entities[mid];
        usize n = strlen(e->name);
        int cmp = memcmp(s + 1, e->name, min((usize)len - 2, n));
        if (!cmp) cmp = (len - 2 > n) - (len - 2 < n);
        if (cmp < 0) hi = mid;
        else if (cmp > 0) lo = mid + 1;
        else { out[0] = e->first; out[1] = e->second; return e->second ? 2 : 1; }
    }
    return 0;
}
