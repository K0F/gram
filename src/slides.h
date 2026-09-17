#ifndef GRAM_SLIDES_H
#define GRAM_SLIDES_H

#include <stdint.h>

/*
 * JPEG/PNG slideshow mode:
 *
 *  - rng mode: scan a directory of pictures, crop/fill them to the
 *    output resolution in grayscale, shuffle with the RNG, and mux with
 *    field recordings as audio. Each image is held for a fixed duration
 *    (default 0.432s, matching the edit span).
 *
 *  - dada mode (opts->dada != 0): all randomness is removed. The stone
 *    (a committed byte image, e.g. dada/noise.png) is the sole source of
 *    entropy; every selection and mix parameter is derived from it, so
 *    identical inputs replay this film byte-for-byte. The film is a fixed
 *    60-second run: an opening title card (stone + opts->title, gomotor
 *    stroke font) then a picture per slide. Picture for slide n is picked
 *    from the sorted pool as bytes(n) % poolsize — same folder, same
 *    film; new files rerandomise. Field audio is a deterministic dense
 *    mix rendered through the EDL engine, padded to 60s.
 */

#define SLIDES_DEFAULT_DUR 0.432
#define SLIDES_DADA_SEC 60.0

typedef struct {
    int   dada;              /* stone-driven deterministic mode */
    int   title_slots;       /* leading stone+title slots (default 6) */
    const char *title;       /* opening title text (e.g. "kof26") */
    const char *stone;       /* stone image path (default dada/noise.png) */
    const char *dada_bin;    /* dada binary (default: exe/dada/dada, cwd/dada/dada, $PATH) */
    double title_px;         /* cap height of the drawn title (default 12px) */
} SlidesOpts;

int slides_run(const char *img_dir, const char *fld_dir, const char *out_mp4,
               int w, int h, int fps, double dur, uint64_t seed,
               int max_images, int mute, const SlidesOpts *opts);

#endif