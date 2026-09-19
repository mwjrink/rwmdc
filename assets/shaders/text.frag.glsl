#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_ARB_shading_language_include : require

#include "types.glsl"

layout(buffer_reference, scalar, buffer_reference_align = 4) restrict readonly buffer GlyphBands_ptr
{
    GlyphBands data;
};

layout(buffer_reference, scalar, buffer_reference_align = 4) restrict readonly buffer PackedCurve_ptr
{
    PackedCurve data;
};

layout(buffer_reference, scalar, buffer_reference_align = 4) restrict readonly buffer Band_ptr
{
    uint data;
};

layout(push_constant, scalar) uniform TextFragPushConstants
{
    layout(offset = 0) GlyphBands_ptr font_glyph_bands;
    layout(offset = 8) PackedCurve_ptr font_curves;
    layout(offset = 16) Band_ptr font_bands;
} pc_frag;

layout(location = 0) in vec4 in_color;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in flat uvec4 in_glyph;

layout(location = 0) out vec4 out_color;

uvec2 load_band(uint glyph_origin, uint band)
{
    uint header = glyph_origin + band * 2u;
    uvec2 result = uvec2(pc_frag.font_bands.data[header], pc_frag.font_bands.data[header + 1u]);

    result.y += glyph_origin;
    return result;
}

bool load_curve(uint index_word, vec2 sample_uv, out vec4 p12, out vec2 p3)
{
    uint index = pc_frag.font_bands.data[index_word];

    PackedCurve curve = pc_frag.font_curves.data[index];

    p12 = vec4(unpackHalf2x16(curve.p0), unpackHalf2x16(curve.p1)) - vec4(sample_uv, sample_uv);
    p3 = unpackHalf2x16(curve.p2) - sample_uv;

    return true;
}

uint root_code(vec3 p)
{
    uint signs = (p.x < 0.0 ? 1u : 0u)
            | (p.y < 0.0 ? 2u : 0u)
            | (p.z < 0.0 ? 4u : 0u);

    return (0x2E74u >> signs) & 0x0101u;
}

vec2 solve_roots(vec3 p)
{
    float a = p.x - 2.0 * p.y + p.z;
    float b = p.x - p.y;

    if (abs(a) <= 1.0e-7 * max(abs(b), 1.0))
    {
        return vec2(b != 0.0 ? p.x / (2.0 * b) : 0.0);
    }

    float d = sqrt(max(b * b - a * p.x, 0.0));

    if (d == 0.0)
    {
        return vec2(b / a);
    }

    if (b >= 0.0)
    {
        float q = b + d;
        return vec2(p.x / q, q / a);
    }

    float q = b - d;
    return vec2(q / a, p.x / q);
}

vec2 intersections(vec3 across, vec3 along)
{
    vec2 t = solve_roots(across);

    float a = along.x - 2.0 * along.y + along.z;
    float b = along.x - along.y;

    return (a * t - 2.0 * b) * t + along.x;
}

float coverage(float xcov, float ycov, float xwgt, float ywgt)
{
    float c = max(abs(xcov * xwgt + ycov * ywgt) /
                max(xwgt + ywgt, 1.0 / 65536.0),
            min(abs(xcov), abs(ycov)));

    return clamp(c, 0.0, 1.0);
}

float glyph_coverage(vec2 uv, vec2 pixels_per_unit, uint glyph_origin, uvec2 band_counts, vec4 bnd)
{
    ivec2 band_count = ivec2(band_counts);

    if (any(lessThanEqual(band_count, ivec2(0))))
    {
        return 0.0;
    }

    ivec2 band_index = clamp(ivec2(uv * bnd.xy + bnd.zw), ivec2(0), band_count - 1);

    float xcov = 0.0;
    float xwgt = 0.0;
    float ycov = 0.0;
    float ywgt = 0.0;

    uvec2 hband = load_band(glyph_origin, uint(band_index.y));

    for (uint i = 0u; i < hband.x; i++)
    {
        vec4 p12;
        vec2 p3;

        if (!load_curve(hband.y + i, uv, p12, p3))
        {
            continue;
        }

        if (max(max(p12.x, p12.z), p3.x) * pixels_per_unit.x < -0.5)
        {
            break;
        }

        uint code = root_code(vec3(p12.y, p12.w, p3.y));

        if (code == 0u)
        {
            continue;
        }

        vec2 r = intersections(vec3(p12.y, p12.w, p3.y),
                vec3(p12.x, p12.z, p3.x)) * pixels_per_unit.x;

        if ((code & 1u) != 0u)
        {
            xcov += clamp(r.x + 0.5, 0.0, 1.0);
            xwgt = max(xwgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
        }

        if ((code & 0x100u) != 0u)
        {
            xcov -= clamp(r.y + 0.5, 0.0, 1.0);
            xwgt = max(xwgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
        }
    }

    uvec2 vband = load_band(glyph_origin, uint(band_count.y + band_index.x));

    for (uint i = 0u; i < vband.x; i++)
    {
        vec4 p12;
        vec2 p3;

        if (!load_curve(vband.y + i, uv, p12, p3))
        {
            continue;
        }

        if (max(max(p12.y, p12.w), p3.y) * pixels_per_unit.y < -0.5)
        {
            break;
        }

        uint code = root_code(vec3(p12.x, p12.z, p3.x));

        if (code == 0u)
        {
            continue;
        }

        vec2 r = intersections(vec3(p12.x, p12.z, p3.x),
                vec3(p12.y, p12.w, p3.y)) * pixels_per_unit.y;

        if ((code & 1u) != 0u)
        {
            ycov -= clamp(r.x + 0.5, 0.0, 1.0);
            ywgt = max(ywgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
        }

        if ((code & 0x100u) != 0u)
        {
            ycov += clamp(r.y + 0.5, 0.0, 1.0);
            ywgt = max(ywgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
        }
    }

    return coverage(xcov, ycov, xwgt, ywgt);
}

void main()
{
    out_color = in_color;

    uint glyph_index = in_glyph.x;

    GlyphBands glyph = pc_frag.font_glyph_bands.data[glyph_index];

    vec2 units_per_pixel = max(fwidth(in_uv), vec2(1.0e-7));
    vec2 pixels_per_unit = 1.0 / units_per_pixel;

    vec4 bnd = vec4(glyph.scale_x, glyph.scale_y, glyph.offset_x, glyph.offset_y);
    uvec2 band_counts = uvec2(glyph.counts & 0xffffu, glyph.counts >> 16);

    float alpha = glyph_coverage(in_uv, pixels_per_unit, glyph.origin, band_counts, bnd);

    if ((in_glyph.y & 0x80000000u) != 0u)
    {
        vec2 dx = dFdx(in_uv) * 0.35;
        vec2 dy = dFdy(in_uv) * 0.35;

        alpha = max(alpha, glyph_coverage(in_uv + dx, pixels_per_unit, glyph.origin, band_counts, bnd));
        alpha = max(alpha, glyph_coverage(in_uv - dx, pixels_per_unit, glyph.origin, band_counts, bnd));
        alpha = max(alpha, glyph_coverage(in_uv + dy, pixels_per_unit, glyph.origin, band_counts, bnd));
        alpha = max(alpha, glyph_coverage(in_uv - dy, pixels_per_unit, glyph.origin, band_counts, bnd));
    }

    out_color = vec4(in_color.rgb, in_color.a * alpha);
}
