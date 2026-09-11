#version 460
#extension GL_EXT_buffer_reference : require

// Slug's analytic quadratic ray coverage, with stable ordered roots.
layout(location = 0) in vec4 colorIn;
layout(location = 1) in vec2 uv;
layout(location = 2) in flat vec4 bnd;
layout(location = 3) in flat uvec4 glyph;
layout(location = 4) in flat uint kind;
layout(location = 0) out vec4 outColor;

struct SlugInstance { float x, y, sx, sy; uint glyph_idx, color; };
struct RectData { float x, y, width, height; uint color, flags; };
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

uvec2 loadBand(uint band) {
    uint words = pc.font.band_words;
    uint origin = glyph.x;
    if (origin > words || words - origin < 2u || band > (words - origin - 2u) / 2u)
        return uvec2(0u);
    uint header = origin + band * 2u;
    uvec2 result = uvec2(pc.font.bands.data[header], pc.font.bands.data[header + 1u]);
    if (result.y > words - origin || result.x > words - origin - result.y)
        return uvec2(0u);
    result.y += origin;
    return result;
}

bool loadCurve(uint indexWord, vec2 sampleUV, out vec4 p12, out vec2 p3) {
    uint index = pc.font.bands.data[indexWord];
    if (index >= pc.font.curve_count) return false;
    PackedCurve curve = pc.font.curves.data[index];
    p12 = vec4(unpackHalf2x16(curve.p0), unpackHalf2x16(curve.p1)) - vec4(sampleUV, sampleUV);
    p3 = unpackHalf2x16(curve.p2) - sampleUV;
    return true;
}

uint rootCode(vec3 p) {
    // Bits 0 and 8 select the two oriented roots. Comparisons canonicalize
    // signed zero so shared contour endpoints receive identical treatment.
    uint signs = (p.x < 0.0 ? 1u : 0u) | (p.y < 0.0 ? 2u : 0u) |
                 (p.z < 0.0 ? 4u : 0u);
    return (0x2E74u >> signs) & 0x0101u;
}

vec2 solveRoots(vec3 p) {
    // a*t*t - 2*b*t + c = 0. Preserve the reference root ordering,
    // but calculate the small root through the product c/a to avoid
    // cancellation near endpoints and on almost-linear quadratics.
    float a = p.x - 2.0 * p.y + p.z;
    float b = p.x - p.y;
    if (abs(a) <= 1.0e-7 * max(abs(b), 1.0)) {
        return vec2(b != 0.0 ? p.x / (2.0 * b) : 0.0);
    }
    float d = sqrt(max(b * b - a * p.x, 0.0));
    if (d == 0.0) return vec2(b / a);
    if (b >= 0.0) {
        float q = b + d;
        return vec2(p.x / q, q / a);
    }
    float q = b - d;
    return vec2(q / a, p.x / q);
}

vec2 intersections(vec3 across, vec3 along) {
    vec2 t = solveRoots(across);
    float a = along.x - 2.0 * along.y + along.z;
    float b = along.x - along.y;
    return (a * t - 2.0 * b) * t + along.x;
}

float coverage(float xcov, float ycov, float xwgt, float ywgt) {
    // Signed accumulation preserves holes under the TrueType nonzero rule.
    // The second term keeps solid interiors covered when neither ray is
    // close enough to an edge to receive an antialiasing weight.
    float c = max(abs(xcov * xwgt + ycov * ywgt) /
                  max(xwgt + ywgt, 1.0 / 65536.0), min(abs(xcov), abs(ycov)));
    return clamp(c, 0.0, 1.0);
}

float glyphCoverage(vec2 uv, vec2 pixelsPerUnit) {
    // glyph.z/w are vertical/horizontal band counts, not inclusive maxima.
    ivec2 bandCount = ivec2(glyph.zw);
    if (any(lessThanEqual(bandCount, ivec2(0)))) return 0.0;
    ivec2 bandIndex = clamp(ivec2(uv * bnd.xy + bnd.zw), ivec2(0), bandCount - 1);
    float xcov = 0.0, xwgt = 0.0;
    float ycov = 0.0, ywgt = 0.0;

    // Band headers hold full-width count/relative-word-offset pairs;
    // each list element is a linear quadratic index.
    uvec2 hband = loadBand(uint(bandIndex.y));
    for (uint i = 0u; i < hband.x; i++) {
        vec4 p12;
        vec2 p3;
        if (!loadCurve(hband.y + i, uv, p12, p3)) continue;
        if (max(max(p12.x, p12.z), p3.x) * pixelsPerUnit.x < -0.5) break;
        uint code = rootCode(vec3(p12.y, p12.w, p3.y));
        if (code == 0u) continue;
        vec2 r = intersections(vec3(p12.y, p12.w, p3.y),
                               vec3(p12.x, p12.z, p3.x)) * pixelsPerUnit.x;
        if ((code & 1u) != 0u) {
            xcov += clamp(r.x + 0.5, 0.0, 1.0);
            xwgt = max(xwgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
        }
        if ((code & 0x100u) != 0u) {
            xcov -= clamp(r.y + 0.5, 0.0, 1.0);
            xwgt = max(xwgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
        }
    }

    uvec2 vband = loadBand(uint(bandCount.y + bandIndex.x));
    for (uint i = 0u; i < vband.x; i++) {
        vec4 p12;
        vec2 p3;
        if (!loadCurve(vband.y + i, uv, p12, p3)) continue;
        if (max(max(p12.y, p12.w), p3.y) * pixelsPerUnit.y < -0.5) break;
        uint code = rootCode(vec3(p12.x, p12.z, p3.x));
        if (code == 0u) continue;
        vec2 r = intersections(vec3(p12.x, p12.z, p3.x),
                               vec3(p12.y, p12.w, p3.y)) * pixelsPerUnit.y;
        if ((code & 1u) != 0u) {
            ycov -= clamp(r.x + 0.5, 0.0, 1.0);
            ywgt = max(ywgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
        }
        if ((code & 0x100u) != 0u) {
            ycov += clamp(r.y + 0.5, 0.0, 1.0);
            ywgt = max(ywgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
        }
    }
    return coverage(xcov, ycov, xwgt, ywgt);
}

void main() {
    if (kind != 0u) {
        outColor = colorIn;
        return;
    }
    vec2 unitsPerPixel = max(fwidth(uv), vec2(1.0e-7));
    vec2 pixelsPerUnit = 1.0 / unitsPerPixel;
    float alpha = glyphCoverage(uv, pixelsPerUnit);
    if ((glyph.y & 0x80000000u) != 0u) {
        // Synthetic optical weight: a 0.35px cross dilation of the same
        // analytic coverage. The unstyled path never changes coverage math.
        vec2 dx = dFdx(uv) * 0.35;
        vec2 dy = dFdy(uv) * 0.35;
        alpha = max(alpha, glyphCoverage(uv + dx, pixelsPerUnit));
        alpha = max(alpha, glyphCoverage(uv - dx, pixelsPerUnit));
        alpha = max(alpha, glyphCoverage(uv + dy, pixelsPerUnit));
        alpha = max(alpha, glyphCoverage(uv - dy, pixelsPerUnit));
    }
    outColor = vec4(colorIn.rgb, colorIn.a * alpha);
}
