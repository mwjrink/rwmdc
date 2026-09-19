#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_ARB_shading_language_include : require

#include "types.glsl"

layout(buffer_reference, scalar, buffer_reference_align = 4) restrict readonly buffer GlyphDrawCmd_ptr
{
    GlyphDrawCmd data;
};

layout(buffer_reference, scalar, buffer_reference_align = 4) restrict readonly buffer GlyphBounds_ptr
{
    GlyphBounds data;
};

layout(push_constant, scalar) uniform TextVertPushConstants
{
    layout(offset = 0)  GlyphDrawCmd_ptr draw_glyphs;
    layout(offset = 8)  GlyphBounds_ptr font_glyphs_bounds;
    layout(offset = 16) vec2 pixel_to_ndc;
    layout(offset = 24) vec2 scroll_offset;
    layout(offset = 32) uint draw_glyph_count;
} pc_vert;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec2 out_uv;
layout(location = 2) out flat uvec4 out_glyph;

void main()
{
    uint idx = uint(gl_InstanceIndex);
    vec2 corner = vec2(gl_VertexIndex & 1, (gl_VertexIndex >> 1) & 1);

    GlyphDrawCmd instance = pc_vert.draw_glyphs.data[idx];

    uint glyph_index = instance.glyph_idx & 0x00ffffffu;

    GlyphBounds bounds = pc_vert.font_glyphs_bounds.data[glyph_index];

    // A zero-area bbox marks an empty glyph (no ink): cull the whole quad.
    if (bounds.min_x == bounds.max_x && bounds.min_y == bounds.max_y)
    {
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        out_color = vec4(0.0);
        out_uv = vec2(0.0);
        out_glyph = uvec4(0u);
        return;
    }

    if (instance.sx == 0.0 || instance.sy == 0.0)
    {
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        out_color = vec4(0.0);
        out_uv = vec2(0.0);
        out_glyph = uvec4(0u);
        return;
    }

    out_color = unpackUnorm4x8(instance.color);

    uint style = instance.glyph_idx & 0xff000000u;
    out_glyph = uvec4(glyph_index, style, 0u, 0u);

    vec2 lo = vec2(bounds.min_x, bounds.min_y);
    vec2 hi = vec2(bounds.max_x, bounds.max_y);

    bool bold = (style & 0x80000000u) != 0u;
    vec2 padding = (bold ? 1.35 : 1.0) / max(abs(vec2(instance.sx, instance.sy)), vec2(1e-6));

    out_uv = mix(lo - padding, hi + padding, corner);

    vec2 pixel = vec2(instance.x, instance.y) + pc_vert.scroll_offset + out_uv * vec2(instance.sx, instance.sy);

    if ((style & 0x40000000u) != 0u)
    {
        pixel.x += 0.21255656 * out_uv.y * abs(instance.sx);
    }

    gl_Position = vec4(pixel * pc_vert.pixel_to_ndc - 1.0, 0.0, 1.0);
}
