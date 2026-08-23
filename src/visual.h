#ifndef GRAM_VISUAL_H
#define GRAM_VISUAL_H

#include <stdint.h>

/*
 * Software rasterizer for the AV compositor. An RGB framebuffer is
 * blended layer-by-layer; the Omicron operator glyph decides the blend
 * gesture, mirroring the audio-side structure:
 *
 *   +  additive / lighten overlay
 *   -  difference blend
 *   x  multiply (darken)
 *   /  split-screen paste
 *
 * Procedural generators render from the slice's own PCM: an XY vector
 * scope with phosphor persistence (XYScope homage), a waveform strip,
 * and structuralist typography of the expression itself.
 */

typedef struct {
    int w, h;
    uint8_t *px;          /* rgb24, w*h*3 */
} Frame;

typedef struct {
    float *acc;           /* persistence buffer, w*h */
    int w, h;
} Scope;

Frame *frame_new(int w, int h);
void frame_free(Frame *f);
void frame_clear(Frame *f, uint8_t r, uint8_t g, uint8_t b);

/* composite src onto dst; a = global alpha 0..1; op = '+','-','x','/' */
void frame_blend(Frame *dst, const Frame *src, char op, float a);

/* multiply whole frame by exposure g */
void frame_expose(Frame *f, float g);
/* vertical fade to black at both ends over n frames each */
void frame_edge_fade(Frame *f, int n_top, int n_bottom, int idx);

Scope *scope_new(int w, int h);
void scope_free(Scope *s);
void scope_reset(Scope *s);

/* draw one frame of the XY scope from interleaved stereo s16 PCM.
 * t0 = absolute sample offset into pcm, nsamp = pcm length in frames. */
void scope_render(Scope *sc, Frame *out, const int16_t *pcm, int64_t nframes,
                  double sr, double local_t, double window_s);

/* waveform strip envelope drawn from PCM around local_t */
void wave_render(Frame *out, const int16_t *pcm, int64_t nframes,
                 double sr, double local_t, double window_s, uint8_t r, uint8_t g, uint8_t b);

/* structuralist title card: greek letter + operator glyph field.
 * seed selects the arrangement deterministically. */
void glyph_render(Frame *out, unsigned seed);

#endif
