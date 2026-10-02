#ifndef GRAM_TITLE_H
#define GRAM_TITLE_H

/*
 * Gomotor intertitle cards: white stroke-font text on black (or over the
 * greyscale stone), rendered at the output raster to a silent/video mp4.
 * This is the C replacement for scripts/title.sh.
 */

/* autofit clamp: pick the cap height that fits the widest line on the
 * raster; widest100 is the rendered advance width of the longest line at
 * cap height 100 px (passed in so the caller can probe once). */
int title_fit_size(int raster_w, float widest100);

/* lines: title text, one string per line (uppercased by the font).
 * stone: optional background image (decoded cover-crop + grayscale),
 * NULL/empty = black. size_px <= 0 = auto-fit to the longest line. */
int title_run(const char *const *lines, int nlines, const char *stone,
              const char *out_mp4, int w, int h, int fps, double dur,
              int size_px, int mute);

#endif