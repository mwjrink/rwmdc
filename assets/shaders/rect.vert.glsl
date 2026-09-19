#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_ARB_shading_language_include : require

#include "types.glsl"

layout(buffer_reference, scalar, buffer_reference_align = 4) restrict readonly buffer GpuRect_ptr
{
    GpuRect data;
};

layout(push_constant, scalar) uniform RectPushConstants
{
    layout(offset = 0)  GpuRect_ptr rects;
    layout(offset = 8)  vec2 pixel_to_ndc;
    layout(offset = 16) vec2 scroll_offset;
    layout(offset = 24) uint rect_count;
} pc_rect;

layout(location = 0) out vec4 out_color;

void main()
{
    uint idx = uint(gl_InstanceIndex);
    vec2 corner = vec2(gl_VertexIndex & 1, (gl_VertexIndex >> 1) & 1);

    GpuRect rect = pc_rect.rects.data[idx];

    vec2 pixel = vec2(rect.x, rect.y) + corner * vec2(rect.width, rect.height);

    // Fixed rects (UI: scrollbar, etc.) ignore scroll; everything else scrolls.
    if ((rect.flags & 1u) == 0u)
    {
        pixel += pc_rect.scroll_offset;
    }

    out_color = unpackUnorm4x8(rect.color);

    gl_Position = vec4(pixel * pc_rect.pixel_to_ndc - 1.0, 0.0, 1.0);
}
