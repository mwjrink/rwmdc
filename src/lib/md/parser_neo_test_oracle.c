/* Read-only MD4C baseline, compiled separately to avoid sharing parser types. */
#define RWMD_MARKDOWN_IMPLEMENTATION
#include "parser.h"
#include <cmark-gfm.h>

/* The result contains success followed by the recognizer's defined outputs. */
void md_neo_test_oracle(unsigned kind, const char* text, uint32_t length,
                        uint32_t flags, uint32_t parameter, uint32_t out[8]) {
    MD_CTX ctx = {0};
    ctx.text = text;
    ctx.size = length;
    ctx.flags = flags;
    ctx.code_indent_offset = flags & MD_FLAG_NOINDENTEDCODEBLOCKS ? UINT32_MAX : 4;
    uint32_t end = 0, begin = 0, value = 0, killer = 0;
    int success = 0;
    memset(out, 0, 8 * sizeof(*out));
    switch (kind) {
        case 0:
            if (length && (text[0] == '-' || text[0] == '_' || text[0] == '*')) {
                success = md_is_hr_line(&ctx, 0, &end, &killer);
                if (!success)
                    out[1] = killer;
            }
            break;
        case 1:
            if (length && text[0] == '#')
                success = md_is_atxheader_line(&ctx, 0, &begin, &end, &value);
            break;
        case 2:
            if (length && (text[0] == '=' || text[0] == '-'))
                success = md_is_setext_underline(&ctx, 0, &end, &value);
            break;
        case 3:
            success = md_is_table_underline(&ctx, 0, &end, &value);
            break;
        case 4:
            if (length && (text[0] == '`' || text[0] == '~')) {
                success = md_is_opening_code_fence(&ctx, 0, &end);
                value = ctx.code_fence_length;
            }
            break;
        case 5:
            ctx.code_fence_length = parameter;
            success = md_is_closing_code_fence(&ctx, (char)flags, 0, &end);
            out[1] = end;
            break;
        case 6: {
            MD_CONTAINER container = {0};
            success = md_is_container_mark(&ctx, parameter, 0, &end, &container);
            if (success) {
                out[4] = container.ch;
                out[5] = container.start;
                out[6] = container.mark_indent;
                out[7] = container.contents_indent;
            }
            break;
        }
        case 7:
            if (length && text[0] == '&')
                success = md_is_entity(&ctx, 0, length, &end);
            break;
        case 8:
            if (length)
                success = md_is_link_destination(&ctx, 0, length, &end, &begin, &value);
            break;
        case 9: {
            int mailto = 0;
            if (length && text[0] == '<')
                success = md_is_autolink(&ctx, 0, length, &end, &mailto);
            value = (unsigned)mailto;
            break;
        }
    }
    out[0] = success != 0;
    if (success) {
        out[1] = end;
        out[2] = begin;
        out[3] = value;
    }
}

typedef struct MdOracleOutput {
    uint32_t* records;
    uint32_t count;
    uint32_t capacity;
} MdOracleOutput;

static int md_oracle_event(const MdEvent* event, void* pointer) {
    MdOracleOutput* output = pointer;
    if (output->count == output->capacity)
        return -1;
    uint32_t* record = output->records + output->count++ * 8;
    record[0] = event->kind;
    record[1] = event->type;
    record[2] = event->source.begin;
    record[3] = event->source.end;
    record[4] = event->flags;
    record[5] = event->codepoint;
    record[6] = event->repeat;
    record[7] = event->level;
    return 0;
}

uint32_t md_neo_document_oracle(const char* source, uint32_t length, uint32_t flags,
                               uint32_t* records, uint32_t capacity) {
    MdParser parser = {0};
    MdOracleOutput output = {.records = records, .capacity = capacity};
    int result = md_parser_parse(&parser, source, length, flags, md_oracle_event, &output, NULL);
    md_parser_destroy(&parser);
    return result ? UINT32_MAX : output.count;
}

uint32_t md_neo_commonmark_code(const char* source, uint32_t length, char* bytes,
                                uint32_t capacity, uint32_t* lengths, uint32_t slots) {
    cmark_node* document = cmark_parse_document(source, length, CMARK_OPT_DEFAULT);
    if (!document)
        return UINT32_MAX;
    cmark_iter* iterator = cmark_iter_new(document);
    uint32_t count = 0, used = 0;
    cmark_event_type event;
    while ((event = cmark_iter_next(iterator)) != CMARK_EVENT_DONE) {
        cmark_node* node = cmark_iter_get_node(iterator);
        if (event == CMARK_EVENT_ENTER && cmark_node_get_type(node) == CMARK_NODE_CODE) {
            const char* value = cmark_node_get_literal(node);
            size_t n = strlen(value);
            if (count == slots || n > capacity - used) {
                count = UINT32_MAX;
                break;
            }
            lengths[count++] = (uint32_t)n;
            memcpy(bytes + used, value, n);
            used += n;
        }
    }
    cmark_iter_free(iterator);
    cmark_node_free(document);
    return count;
}
