#ifndef GRAM_PLAN_H
#define GRAM_PLAN_H

#include <stdint.h>

#include "library.h"
#include "util.h"

/*
 * Composition planning. Two engines:
 *
 *   rng     — faithful port of michacka: seeded xorshift picks slices,
 *             volumes, fades, jitter inside style envelopes.
 *   omicron — OperatorOmikron as structure driver: enumerated operator
 *             expressions (letters 1..N, ops + − × ÷) deterministically map
 *             onto track selection, slice points, gains, fades and the
 *             master arc. No randomness at all.
 *
 * Both engines emit tj-compatible EDL strings plus a .vedl sidecar mapping
 * every music entry to its visual gesture (operator glyph + generator).
 */

#define MAX_EDL_ENTRIES 30
#define PLAN_MARGIN 2.0

enum { ST_DAY, ST_STORM, ST_DRIFT, ST_PULSE, ST_RUPTURE };
enum { PLAN_ENGINE_RNG, PLAN_ENGINE_OMICRON };

typedef struct {
    const char *name;
    int def_parts;
    float def_len;
    EnvPt beds[10];    int nb;
    EnvPt motion[10];  int nm;
    EnvPt pulses[10];  int np;
    EnvPt fields[10];  int nfl;
    EnvPt gain[10];    int ng;
    float fade_in[2];
    float fade_out[2];
    int parity;
    int bpm;
    int keylock;
} StyleSpec;

typedef struct {
    uint64_t seed;
    int have_seed;
    int parts;
    double part_len;         /* <=0 -> style default */
    int style;
    const char *mus_dir;
    const char *fld_dir;
    char out_prefix[512];
    int dry_run;
    int engine;
    int omicron_letters;     /* default 8 */
    int has_target;
    double target;
    int max_files;           /* library sampling cap (default 1000) */
    int av;                  /* write .vedl sidecars */
} PlanCfg;

typedef struct {
    char **music_edls;       /* [parts] */
    char **field_edls;       /* [parts] */
    char **arcs;             /* [parts] */
    char **vedls;            /* [parts] newline-separated "<idx> <op> <gen>", or NULL */
} PlanResult;

const StyleSpec *plan_style(int idx);
int plan_style_by_name(const char *name);
int plan_layer_count(const StyleSpec *st, const EnvPt *e, int n, float phase, int part_idx);

/* fill defaults for unset cfg fields (style default length etc.) */
void plan_cfg_defaults(PlanCfg *cfg);

/* run planning; result arrays are xmalloc'd, caller frees via plan_free */
void plan_run(const PlanCfg *cfg, TrackList *mus, TrackList *fld, PlanResult *out);
void plan_free(const PlanCfg *cfg, PlanResult *out);

#endif
