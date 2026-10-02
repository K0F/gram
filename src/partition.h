#ifndef GRAM_PARTITION_H
#define GRAM_PARTITION_H

#include <stddef.h>
#include <stdint.h>

/*
 * Transport-edit DSL (scripts/partition.sh lineage):
 *
 *   open K   switch to source K (0-based), reset position to its start
 *   jf N     jump forward  N units (no output)
 *   jb N     jump backward N units, clamp to the start of the current source
 *   pf N     play forward  N units into the edit
 *   x M      repeat the previous verb M times (consecutive chunks)
 *   H n m    n harmonic-ratio sections scaled to the current source's length;
 *            in each section play m rapid one-unit cuts scanned across it
 *
 * All frame arithmetic lives in a 25 fps virtual space (unit = frames per
 * unit, default 11 ~ 0.432 s). In pattern mode the sources form one virtual
 * concatenation; in script mode each 'open' block is bounded to its source.
 */

typedef struct {
    int src;         /* source index into the caller's srcs[] array */
    long long local; /* start frame within that source */
    long long len;   /* frames */
} PCut;

/* combined-space inclusive-exclusive segment (start, end) */
typedef struct {
    long long b, e;
} PSeg;

/* token stream from part_lex; word[] is valid for VERB/KW/ERR tokens */
enum { PT_EOF = 0, PT_NUM, PT_X, PT_KW, PT_VERB, PT_ERR };

typedef struct {
    int type;
    long long num;
    char word[16];
} PTok;

/* lex a DSL body into tokens ending in PT_EOF; returns token count
 * (incl. EOF), or -1 on overflow. ERR words are emitted, not rejected. */
int part_lex(const char *text, PTok *toks, int cap);

/* interpret tokens against per-source frame arrays (25 fps virtual).
 * lens/full/offs/nf: frames per source, full frames, combined offsets;
 * require_open: script mode demands an 'open' before any op; unit = frames
 * per unit. Appends combined segments to seg out of segcap. */
int part_interp(const PTok *toks, int ntok,
                const long long *lens, const long long *full,
                const long long *offs, int nf, int require_open,
                long long unit, PSeg *seg, int segcap);

/* scan a script for 'open <file>' lines; returns realpath'd deduped list.
 * Returns 0 ok, -1 on error. Caller frees *out and its strings. */
int part_script_open_sources(const char *script, char ***out, int *n);

/* full pipeline: lex + interpret 'pattern' or 'script' over sources and
 * map combined cuts onto (src, local). -1 on error; 0 ok. */
int part_plan(const char *pattern, const char *script,
              const char *const *srcs, int nsrcs, int unit, double fps,
              PCut **cuts_out, int *ncuts_out, double *total_sec_out);

/* outputs */
int part_write_edl(const PCut *cuts, int ncuts,
                   const char *const *srcs, int nsrcs,
                   const char *path, int fps);

/* MLT project XML; std != 0 standardizes sources to the output raster
 * (cached in ~/.cache/partition_std) before referencing them. */
int part_write_mlt(const PCut *cuts, int ncuts,
                   const char *const *srcs, int nsrcs,
                   const char *path, int w, int h, int fps, int std);

/* native silent render: cuts become vfile entries through av_render. */
int part_render(const PCut *cuts, int ncuts,
                const char *const *srcs, int nsrcs,
                const char *out_mp4, int w, int h, int fps);

#endif