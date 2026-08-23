#include "visual.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Frame *frame_new(int w, int h)
{
    Frame *f = malloc(sizeof(Frame));
    if (!f) return NULL;
    f->w = w;
    f->h = h;
    f->px = malloc((size_t)w * h * 3);
    if (!f->px) { free(f); return NULL; }
    frame_clear(f, 0, 0, 0);
    return f;
}

void frame_free(Frame *f)
{
    if (!f) return;
    free(f->px);
    free(f);
}

void frame_clear(Frame *f, uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < f->w * f->h; i++) {
        f->px[i * 3 + 0] = r;
        f->px[i * 3 + 1] = g;
        f->px[i * 3 + 2] = b;
    }
}

static inline uint8_t clamp255(int v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v);
}

void frame_blend(Frame *dst, const Frame *src, char op, float a)
{
    size_t n = (size_t)dst->w * dst->h * 3;
    if (a >= 1.0f) {
        switch (op) {
        case '+':
            for (size_t i = 0; i < n; i++)
                dst->px[i] = clamp255(dst->px[i] + src->px[i]);
            return;
        case '-':
            for (size_t i = 0; i < n; i++) {
                int d = dst->px[i] - src->px[i];
                dst->px[i] = (uint8_t)(d < 0 ? -d : d);
            }
            return;
        case 'x':
            for (size_t i = 0; i < n; i++)
                dst->px[i] = (uint8_t)((dst->px[i] * src->px[i]) / 255);
            return;
        case '/':
            /* split screen: right half from source */
            for (int y = 0; y < dst->h; y++) {
                memcpy(dst->px + ((size_t)y * dst->w + dst->w / 2) * 3,
                       src->px + ((size_t)y * src->w + src->w / 2) * 3,
                       (size_t)(dst->w / 2) * 3);
            }
            return;
        default:
            return;
        }
    }
    /* faded variant */
    for (size_t i = 0; i < n; i++) {
        float s = src->px[i] * a;
        float d = dst->px[i];
        float v;
        switch (op) {
        case '+': v = d + s; break;
        case '-': v = fabsf(d - s); break;
        case 'x': v = (d * s) / 255.0f; break;
        case '/': v = d; break;
        default: v = d; break;
        }
        dst->px[i] = clamp255((int)v);
    }
    if (op == '/' && a > 0.5f) {
        for (int y = 0; y < dst->h; y++)
            memcpy(dst->px + ((size_t)y * dst->w + dst->w / 2) * 3,
                   src->px + ((size_t)y * src->w + src->w / 2) * 3,
                   (size_t)(dst->w / 2) * 3);
    }
}

void frame_expose(Frame *f, float g)
{
    size_t n = (size_t)f->w * f->h * 3;
    for (size_t i = 0; i < n; i++)
        f->px[i] = clamp255((int)(f->px[i] * g));
}

void frame_edge_fade(Frame *f, int n_top, int n_bottom, int idx)
{
    if (idx < n_top) {
        float a = (float)(idx + 1) / (float)n_top;
        frame_expose(f, a * a);
    } else if (idx >= n_bottom) {
        float a = (float)(n_bottom + n_top - idx) / (float)n_top;
        frame_expose(f, a * a);
    }
}

/* ------------------------------------------------------------------ */

Scope *scope_new(int w, int h)
{
    Scope *s = malloc(sizeof(Scope));
    if (!s) return NULL;
    s->w = w;
    s->h = h;
    s->acc = calloc((size_t)w * h, sizeof(float));
    if (!s->acc) { free(s); return NULL; }
    return s;
}

void scope_free(Scope *s)
{
    if (!s) return;
    free(s->acc);
    free(s);
}

void scope_reset(Scope *s)
{
    memset(s->acc, 0, (size_t)s->w * s->h * sizeof(float));
}

/* plot a beam segment into the persistence buffer with additive energy */
static void scope_plot(Scope *s, float x0, float y0, float x1, float y1, float e)
{
    int steps = (int)fmaxf(1.0f, fmaxf(fabsf(x1 - x0), fabsf(y1 - y0)));
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / steps;
        int x = (int)((x0 + (x1 - x0) * t) * s->w);
        int y = (int)((y0 + (y1 - y0) * t) * s->h);
        if (x < 0 || x >= s->w || y < 0 || y >= s->h) continue;
        float *p = &s->acc[(size_t)y * s->w + x];
        *p += e;
        if (*p > 4.0f) *p = 4.0f;
    }
}

void scope_render(Scope *sc, Frame *out, const int16_t *pcm, int64_t nframes,
                  double sr, double local_t, double window_s_unused)
{
    (void)window_s_unused;
    /* phosphor decay */
    float decay = 0.80f;
    size_t np = (size_t)sc->w * sc->h;
    for (size_t i = 0; i < np; i++) sc->acc[i] *= decay;

    int64_t center = (int64_t)(local_t * sr) * 2;
    if (center < 0) center = 0;
    int span = 4096;
    int64_t start = center - span / 2;
    if (start < 0) start = 0;
    if (start + span * 2 > nframes * 2) start = nframes * 2 - span * 2;
    if (start < 0) start = 0;

    float px = 0, py = 0;
    for (int i = 0; i < span; i++) {
        int64_t li = start + i * 2;
        int64_t ri = li + 1;
        if (li + 1 >= nframes * 2) break;
        float x = 0.5f + ((float)pcm[li] / 32768.0f) * 0.48f;
        float y = 0.5f + ((float)pcm[ri] / 32768.0f) * 0.48f;
        if (i > 0)
            scope_plot(sc, px, py, x, y, 0.55f);
        px = x; py = y;
    }

    /* tonemap persistence to green phosphor */
    for (size_t i = 0; i < np; i++) {
        float v = 1.0f - expf(-sc->acc[i] * 1.6f);
        uint8_t g = (uint8_t)(v * 220.0f + 0.5f);
        out->px[i * 3 + 0] = (uint8_t)(g * 0.25f);
        out->px[i * 3 + 1] = g;
        out->px[i * 3 + 2] = (uint8_t)(g * 0.45f);
    }
}

void wave_render(Frame *out, const int16_t *pcm, int64_t nframes,
                 double sr, double local_t, double window_s, uint8_t r, uint8_t g, uint8_t b)
{
    int W = out->w, H = out->h;
    int mid_y = H * 3 / 4;

    /* clear with slight trail */
    for (size_t i = 0; i < (size_t)W * H * 3; i += 3) {
        out->px[i + 0] /= 2;
        out->px[i + 1] /= 2;
        out->px[i + 2] /= 2;
    }

    int64_t center = (int64_t)(local_t * sr) * 2;
    int64_t half = (int64_t)(window_s * sr);
    int64_t start = center - half;
    if (start < 0) start = 0;
    int64_t end = center + half;
    if (end > nframes) end = nframes;
    if (end <= start) return;

    int cols = W;
    int64_t per_col = (end - start) / cols;
    if (per_col < 1) per_col = 1;
    for (int x = 0; x < cols; x++) {
        float mn = 1e9f, mx = -1e9f;
        int64_t s0 = start + (int64_t)x * per_col;
        for (int64_t j = s0; j < s0 + per_col && j < end; j++) {
            float mono = (pcm[j * 2] + pcm[j * 2 + 1]) * 0.5f / 32768.0f;
            if (mono < mn) mn = mono;
            if (mono > mx) mx = mono;
        }
        if (mn > mx) continue;
        int y0 = (int)((0.5f - mx * 0.35f) * H);
        int y1 = (int)((0.5f - mn * 0.35f) * H);
        if (y0 > mid_y) y0 = mid_y;
        if (y1 < mid_y) y1 = mid_y;
        for (int y = y0; y <= y1; y++) {
            if (y < 0 || y >= H) continue;
            uint8_t *p = out->px + ((size_t)y * W + x) * 3;
            p[0] = r; p[1] = g; p[2] = b;
        }
    }
}

/* 5x7 glyph bitmaps for the structural alphabet */
static const char *glyph_bits(char c)
{
    switch (c) {
    case 'a': return "01110100011011110001";   /* alpha-ish */
    case 'b': return "11100100011010001110";
    case 'g': return "01110100011011010001";
    case 'd': return "11101000100010101110";
    case 'e': return "11101000111010001111";
    case 'z': return "11110001001010001111";
    case '+': return "00000010011110001000";
    case '-': return "00000000011100000000";
    case 'x': return "10001010100010100010";
    case '/': return "00010001001010100001";
    default:  return "00000100001000010000";
    }
}

static void draw_glyph(Frame *out, char c, int ox, int oy, int scale,
                       uint8_t r, uint8_t g, uint8_t b)
{
    const char *bits = glyph_bits(c);
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 4; col++) {
            if (bits[row * 4 + col] != '1') continue;
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    int x = ox + col * scale + dx;
                    int y = oy + row * scale + dy;
                    if (x < 0 || x >= out->w || y < 0 || y >= out->h) continue;
                    uint8_t *p = out->px + ((size_t)y * out->w + x) * 3;
                    p[0] = r; p[1] = g; p[2] = b;
                }
            }
        }
    }
}

void glyph_render(Frame *out, unsigned seed)
{
    static const char letters[] = { 'a', 'b', 'g', 'd', 'e', 'z' };
    static const char ops[] = { '+', '-', 'x', '/' };
    unsigned st = seed * 2654435761u + 1;
    frame_clear(out, 6, 6, 10);
    int cols = 8, rows = 4;
    int cw = out->w / cols, ch = out->h / rows;
    for (int ry = 0; ry < rows; ry++) {
        for (int cx = 0; cx < cols; cx++) {
            st ^= st << 13; st ^= st >> 17; st ^= st << 5;
            char l = letters[st % sizeof(letters)];
            st ^= st << 13; st ^= st >> 17; st ^= st << 5;
            char o = ops[st % sizeof(ops)];
            int scale = ch / 14;
            int ox = cx * cw + (cw - 4 * scale) / 2;
            int oy = ry * ch + (ch - 5 * scale) / 2;
            draw_glyph(out, l, ox, oy, scale, 90, 200, 130);
            draw_glyph(out, o, ox + 6 * scale, oy + scale, scale, 220, 120, 90);
        }
    }
}
