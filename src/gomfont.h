#ifndef GRAM_GOMFONT_H
#define GRAM_GOMFONT_H

#include "visual.h"

/*
 * GoMotor stroke-font rasterizer.
 *
 * The alphabet lives in a 50-unit cap space (y=0 cap top, y=50 baseline,
 * x=0..40 glyph advance, 10-unit inter-character spacing) and is embedded
 * as generated C data from gomotor/gen.go (see tools/gen_gomfont.py).
 * gf_render() rasterizes a text string onto a Frame, centered at (cx, cy)
 * in device pixels, scaled so the cap height equals cap_px.
 */

/* single stroke = contiguous pen polyline in font units */
typedef struct { float x, y; } GfPt;
typedef struct { const GfPt *p; int n; } GfStroke;
typedef struct {
    unsigned cp;               /* unicode code point */
    int nstrokes;
    const GfStroke *s;         /* may be NULL when nstrokes == 0 */
} GfGlyph;

/* draw text, cap-height = cap_px px, centered at (cx,cy), r/g/b 0..255.
 * returns total advance width in px (needed by callers to place text). */
float gf_render(Frame *out, const char *text, float cx, float cy,
                float cap_px, uint8_t r, uint8_t g, uint8_t b);

#endif