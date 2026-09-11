#version 460
#extension GL_EXT_buffer_reference : require

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outUV;
layout(location = 2) out flat vec4 outBnd;
layout(location = 3) out flat uvec4 outGlyph;
layout(location = 4) out flat uint outKind;

struct SlugInstance {
    float x, y, sx, sy;
    uint glyph_idx, color;
};
struct RectData {
    float x, y, width, height;
    uint color, flags;
};
struct SlugGlyphData {
    float min_x, min_y, max_x, max_y;
    float band_scale_x, band_scale_y, band_offset_x, band_offset_y;
    uint band_origin, band_counts;
};
struct PackedCurve { uint p0, p1, p2; };
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer InstanceBuffer { SlugInstance data[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer RectBuffer { RectData data[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer GlyphBuffer { SlugGlyphData data[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer CurveBuffer { PackedCurve data[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer BandBuffer { uint data[]; };
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer WindowHeader {
    InstanceBuffer glyphs;
    RectBuffer rects;
    uint glyph_count, rect_count;
    vec2 viewport_scale;
    vec4 text_color;
    vec2 cursor_size;
    uint cursor_color, cursor_visible;
};
layout(buffer_reference, std430, buffer_reference_align = 8) readonly buffer FontHeader {
    GlyphBuffer glyphs;
    CurveBuffer curves;
    BandBuffer bands;
    uint glyph_count, curve_count, band_words, reserved;
};
layout(push_constant, std430) uniform PushConstants {
    WindowHeader window;
    FontHeader font;
    vec2 scroll_offset;
    vec2 cursor_position;
} pc;

void main() {
    WindowHeader window = pc.window;
    uint index = uint(gl_InstanceIndex);
    vec2 corner = vec2(gl_VertexIndex & 1, (gl_VertexIndex >> 1) & 1);
    outUV = vec2(0.0);
    outBnd = vec4(0.0);
    outGlyph = uvec4(0u);
    outColor = vec4(0.0);
    outKind = 1u;
    if (index < window.rect_count) {
        RectData rect = window.rects.data[index];
        vec2 pixel = vec2(rect.x, rect.y) + corner * vec2(rect.width, rect.height);
        if ((rect.flags & 1u) == 0u) pixel += pc.scroll_offset;
        outColor = unpackUnorm4x8(rect.color);
        gl_Position = vec4(pixel * window.viewport_scale - 1.0, 0.0, 1.0);
        return;
    }
    index -= window.rect_count;
    if (index >= window.glyph_count) {
        if (index != window.glyph_count || window.cursor_visible == 0u) {
            gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
            return;
        }
        vec2 pixel = pc.cursor_position + pc.scroll_offset + corner * window.cursor_size;
        outColor = unpackUnorm4x8(window.cursor_color);
        gl_Position = vec4(pixel * window.viewport_scale - 1.0, 0.0, 1.0);
        return;
    }
    SlugInstance inst = window.glyphs.data[index];
    uint glyphIndex = inst.glyph_idx & 0x00ffffffu;
    if (glyphIndex >= pc.font.glyph_count) {
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        return;
    }
    SlugGlyphData glyph = pc.font.glyphs.data[glyphIndex];
    outKind = 0u;
    outColor = unpackUnorm4x8(inst.color) * window.text_color;
    outGlyph = uvec4(glyph.band_origin, inst.glyph_idx & 0xff000000u,
                     glyph.band_counts & 65535u, glyph.band_counts >> 16);
    outBnd = vec4(glyph.band_scale_x, glyph.band_scale_y,
                  glyph.band_offset_x, glyph.band_offset_y);
    if (glyph.band_counts == 0u || inst.sx == 0.0 || inst.sy == 0.0) {
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        return;
    }
    vec2 lo = vec2(glyph.min_x, glyph.min_y);
    vec2 hi = vec2(glyph.max_x, glyph.max_y);
    // Normal glyphs retain exactly the old one-pixel bounds and transform.
    bool bold = (inst.glyph_idx & 0x80000000u) != 0u;
    vec2 padding = (bold ? 1.35 : 1.0) / max(abs(vec2(inst.sx, inst.sy)), vec2(1e-6));
    outUV = mix(lo - padding, hi + padding, corner);
    vec2 pixel = vec2(inst.x, inst.y) + pc.scroll_offset + outUV * vec2(inst.sx, inst.sy);
    // Synthetic italic: twelve-degree rightward shear in font coordinates.
    if ((inst.glyph_idx & 0x40000000u) != 0u) pixel.x += 0.21255656 * outUV.y * abs(inst.sx);
    gl_Position = vec4(pixel * window.viewport_scale - 1.0, 0.0, 1.0);
}
