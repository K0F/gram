#ifndef GRAM_SLIDES_H
#define GRAM_SLIDES_H

#include <stdint.h>

/*
 * JPEG slideshow mode: scan a directory of JPEGs, crop/fill them to the
 * output resolution in grayscale, shuffle with the RNG, and mux with
 * field recordings as audio. Each image is held for a fixed duration
 * (default 0.432s, matching the edit span).
 */

#define SLIDES_DEFAULT_DUR 0.432

int slides_run(const char *img_dir, const char *fld_dir, const char *out_mp4,
               int w, int h, int fps, double dur, uint64_t seed,
               int max_images, int mute);

#endif
