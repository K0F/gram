#include "gomfont.h"

#include <math.h>
#include <string.h>

#include "gomfont_data.c"   /* GF_GLYPHS */

#define X_UNIT  40.0f   /* glyph advance (cap units) */
#define X_GAP   10.0f   /* inter-character spacing (cap units) */

static const GfGlyph *gf_find(unsigned cp)
{
    int lo = 0, hi = (int)(sizeof(GF_GLYPHS) / sizeof(GF_GLYPHS[0])) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (GF_GLYPHS[mid].cp < cp) lo = mid + 1;
        else if (GF_GLYPHS[mid].cp > cp) hi = mid - 1;
        else return &GF_GLYPHS[mid];
    }
    return NULL;
}

/* plot a 1px-ish dot at device position if inside the frame */
static inline void dot(Frame *out, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if (x < 0 || y < 0 || x >= out->w || y >= out->h) return;
    uint8_t *p = out->px + (y * out->w + x) * 3;
    p[0] = r; p[1] = g; p[2] = b;
}

/* rasterize stroke pen polyline: step down the segments and paint small
 * disks so a 12px cap stays legible. pen_r is the disc radius. */
static void stroke_draw(Frame *out, const GfPt *p, int n,
                        float sx, float sy, float scale, float pen_r,
                        uint8_t r, uint8_t g, uint8_t b)
{
    int i;
    for (i = 0; i + 1 < n; i++) {
        float ax = p[i].x, ay = p[i].y, bx = p[i + 1].x, by = p[i + 1].y;
        float dx = bx - ax, dy = by - ay;
        float len = sqrtf(dx * dx + dy * dy) * scale;
        int steps = (int)ceilf(len * 4.0f) + 1;
        int s;
        for (s = 0; s < steps; s++) {
            float t = steps > 1 ? (float)s / (steps - 1) : 0.0f;
            float px = sx + (ax + t * dx) * scale;
            float py = sy + (ay + t * dy) * scale;
            int cx = (int)floorf(px), cy = (int)floorf(py);
            int rw = (int)ceilf(pen_r);
            int a, b2;
            for (a = -rw; a <= rw; a++) {
                for (b2 = -rw; b2 <= rw; b2++) {
                    float dist = hypotf(a, b2);
                    if (dist <= pen_r) dot(out, cx + a, cy + b2, r, g, b);
                }
            }
        }
    }
}

static float gf_text_width(const char *text, float scale, int *out_chars)
{
    float w = 0.0f;
    int chars = 0;
    const unsigned char *s = (const unsigned char *)text;
    for (; *s; s++) {
        w += X_UNIT;
        chars++;
    }
    if (chars > 0) w += X_GAP * (chars - 1);
    if (out_chars) *out_chars = chars;
    return w * scale;
}

float gf_render(Frame *out, const char *text, float cx, float cy,
                float cap_px, uint8_t r, uint8_t g, uint8_t b)
{
    float scale = cap_px / 50.0f;
    int chars = 0;
    float width = gf_text_width(text, scale, &chars);
    float pen_r = 0.45f;
    const unsigned char *s = (const unsigned char *)text;
    float x = cx - width / 2.0f;
    float base_y = cy + cap_px / 2.0f;   /* font-unit baseline at glyph center */
    for (; *s; s++) {
        unsigned cp = *s;
        if (cp >= 'a' && cp <= 'z') cp &= ~0x20u;   /* alphabet is uppercase */
        const GfGlyph *gl = gf_find(cp);
        if (gl && gl->nstrokes > 0) {
            int i;
            for (i = 0; i < gl->nstrokes; i++) {
                const GfStroke *st = &gl->s[i];
                if (st->n > 0)
                    stroke_draw(out, st->p, st->n,
                                x, base_y - 50.0f * scale, scale, pen_r,
                                r, g, b);
            }
        }
        x += (X_UNIT + X_GAP) * scale;
    }
    return width;
}