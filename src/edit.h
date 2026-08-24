#ifndef GRAM_EDIT_H
#define GRAM_EDIT_H

#include <stddef.h>

/*
 * Text-driven deterministic video editor ("edit from a string").
 *
 * The text read from stdin is the score: every letter a..z (= 1..26)
 * picks one clip from the path-sorted video pool via (v-1) mod n, and
 * its position in the text places the cut inside that clip through
 * golden-ratio scatter. Cuts are laid out continuously on the timeline;
 * non-letters are ignored. Same text + same pool -> same edit, byte for
 * byte, no randomness.
 */

#define EDIT_DEFAULT_SPAN 0.432

typedef struct {
    int clip;          /* index into the pool */
    double in_sec;     /* cut start inside the clip */
    double span;       /* slice length */
    double at;         /* timeline placement */
} EditCut;

typedef struct {
    double span;       /* slice length seconds (default 2) */
} EditCfg;

void edit_cfg_defaults(EditCfg *cfg);

/*
 * Map text -> cuts. paths/durs describe the pool (parallel arrays, len n;
 * durs may hold <= 0 for unknown, span is then used unclamped).
 * Only letters emit cuts; everything else (including whitespace) is
 * ignored. Uppercase folds to lowercase.
 * Returns the number of cuts written to out, or -1 if the text needs more
 * than out_cap cuts.
 */
long edit_plan_text(const char *text, char const *const *paths,
                    const double *durs, int n, const EditCfg *cfg,
                    EditCut *out, int out_cap);

/* full pipeline: read stdin, scan vid_dir, render a silent mp4.
 * edl_dump (optional) writes the synthesized EDL next to the render. */
int edit_run(const char *vid_dir, const EditCfg *cfg, const char *out_mp4,
             int w, int h, int fps, int max_files, const char *edl_dump);

#endif
