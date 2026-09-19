struct GlyphDrawCmd
{
    float x;
    float y;
    float sx;
    float sy;

    uint glyph_idx;
    uint color;
};

struct GpuRect
{
    float x;
    float y;
    float width;
    float height;

    uint color;
    uint flags;
};

struct GlyphBounds
{
    float min_x;
    float min_y;
    float max_x;
    float max_y;
};

struct GlyphBands
{
    float scale_x;
    float scale_y;
    float offset_x;
    float offset_y;

    uint origin;
    uint counts;
};

struct PackedCurve
{
    uint p0;
    uint p1;
    uint p2;
};
