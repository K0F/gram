#include "plan.h"

#include "omicron.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const StyleSpec STYLES[] = {
    { "day", 1, 600.0f,
      { {0.00f, 2}, {0.18f, 1}, {0.82f, 1}, {1.00f, 2} }, 4,
      { {0.00f, 1}, {0.25f, 2}, {0.42f, 3}, {0.58f, 3}, {0.78f, 2}, {1.00f, 1} }, 6,
      { {0.00f, 0}, {0.15f, 1}, {0.32f, 3}, {0.55f, 3}, {0.72f, 2}, {0.86f, 0}, {1.00f, 0} }, 7,
      { {0.00f, 2}, {0.38f, 3}, {0.62f, 3}, {0.85f, 2}, {1.00f, 3} }, 5,
      { {0.0f, 0.85f}, {0.5f, 1.0f}, {1.0f, 0.85f} }, 3,
      { 14.0f, 28.0f }, { 16.0f, 28.0f }, 0, 0, 0 },
    { "storm", 2, 300.0f,
      { {0.0f, 1}, {1.0f, 1} }, 2,
      { {0.0f, 2}, {0.25f, 4}, {0.75f, 4}, {1.0f, 2} }, 4,
      { {0.0f, 1}, {0.15f, 4}, {0.85f, 4}, {1.0f, 2} }, 4,
      { {0.0f, 1}, {0.5f, 2}, {1.0f, 1} }, 3,
      { {0.0f, 0.6f}, {0.12f, 1.0f}, {0.82f, 1.0f}, {1.0f, 0.65f} }, 4,
      { 3.0f, 6.0f }, { 4.0f, 8.0f }, 0, 0, 0 },
    { "drift", 1, 600.0f,
      { {0.0f, 2}, {1.0f, 2} }, 2,
      { {0.0f, 1}, {0.5f, 2}, {1.0f, 1} }, 3,
      { {0.0f, 0}, {1.0f, 0} }, 2,
      { {0.0f, 2}, {0.5f, 3}, {1.0f, 2} }, 3,
      { {0.0f, 0.8f}, {0.5f, 0.95f}, {1.0f, 0.8f} }, 3,
      { 18.0f, 35.0f }, { 20.0f, 35.0f }, 0, 0, 0 },
    { "pulse", 1, 600.0f,
      { {0.0f, 1}, {1.0f, 1} }, 2,
      { {0.0f, 2}, {1.0f, 2} }, 2,
      { {0.0f, 3}, {0.15f, 5}, {0.9f, 4}, {1.0f, 3} }, 4,
      { {0.0f, 1}, {0.5f, 2}, {1.0f, 1} }, 3,
      { {0.0f, 0.7f}, {0.1f, 1.0f}, {0.92f, 1.0f}, {1.0f, 0.75f} }, 4,
      { 4.0f, 8.0f }, { 5.0f, 10.0f }, 0, 1, 0 },
    { "rupture", 2, 300.0f,
      { {0.0f, 1}, {1.0f, 1} }, 2,
      { {0.0f, 2}, {0.5f, 3}, {1.0f, 2} }, 3,
      { {0.0f, 1}, {0.5f, 3}, {1.0f, 1} }, 3,
      { {0.0f, 1}, {0.5f, 2}, {1.0f, 1} }, 3,
      { {0.0f, 0.5f}, {0.1f, 1.0f}, {0.75f, 1.0f}, {1.0f, 0.6f} }, 4,
      { 2.0f, 4.0f }, { 3.0f, 6.0f }, 1, 1, 1 },
};

const StyleSpec *plan_style(int idx)
{
    if (idx < 0 || idx >= (int)(sizeof(STYLES) / sizeof(STYLES[0]))) return NULL;
    return &STYLES[idx];
}

int plan_style_by_name(const char *name)
{
    for (size_t i = 0; i < sizeof(STYLES) / sizeof(STYLES[0]); i++)
        if (strcmp(STYLES[i].name, name) == 0) return (int)i;
    return -1;
}

int plan_layer_count(const StyleSpec *st, const EnvPt *e, int n, float phase, int part_idx)
{
    float v = env_eval(e, n, phase);
    if (st->parity) v = (part_idx % 2 == 0) ? v * 1.5f : v * 0.5f;
    int c = (int)lroundf(v);
    return c > MAX_EDL_ENTRIES ? MAX_EDL_ENTRIES : c;
}

void plan_cfg_defaults(PlanCfg *cfg)
{
    const StyleSpec *st = plan_style(cfg->style);
    if (!cfg->part_len || cfg->part_len <= 0) cfg->part_len = st->def_len;
    if (cfg->parts <= 0) cfg->parts = st->def_parts;
    if (cfg->parts < 1 || cfg->parts > 96) die("--parts must be 1..96");
    if (!cfg->omicron_letters) cfg->omicron_letters = 8;
    if (cfg->omicron_letters < 2 || cfg->omicron_letters > OMICRON_MAX_LETTERS)
        die("--letters must be 2..26");
    if (!cfg->max_files) cfg->max_files = 1000;
}

/* ------------------------------------------------------------------ */
/* shared EDL writer                                                   */

typedef struct {
    char *buf;
    size_t len, cap;
    int count;
    char *vedl;              /* newline-separated sidecar lines or NULL */
    size_t vlen, vcap;
} Edl;

static void edl_init(Edl *e, int av)
{
    e->cap = 16384;
    e->buf = xmalloc((size_t)e->cap);
    e->buf[0] = 0;
    e->len = 0;
    e->count = 0;
    e->vedl = NULL;
    e->vlen = 0;
    e->vcap = 0;
    if (av) {
        e->vcap = 1024;
        e->vedl = xmalloc((size_t)e->vcap);
        e->vedl[0] = 0;
    }
}

static void edl_free(Edl *e)
{
    free(e->buf);
    free(e->vedl);
    e->buf = NULL;
    e->vedl = NULL;
}

static void edl_vedl(Edl *e, const char *gen, int op)
{
    if (!e->vedl) return;
    char line[64];
    int w = snprintf(line, sizeof(line), "%d %c %s\n", e->count, omicron_op_char(op), gen);
    if (w < 0) return;
    if (e->vlen + (size_t)w + 1 > e->vcap) {
        while (e->vlen + (size_t)w + 1 > e->vcap) e->vcap *= 2;
        e->vedl = xrealloc(e->vedl, (size_t)e->vcap);
    }
    memcpy(e->vedl + e->vlen, line, (size_t)w + 1);
    e->vlen += (size_t)w;
}

static void edl_put(Edl *e, const char *fmt, ...)
{
    va_list ap;
    char entry[1600];
    va_start(ap, fmt);
    int w = vsnprintf(entry, sizeof(entry), fmt, ap);
    va_end(ap);
    if (w < 0 || (size_t)w >= sizeof(entry)) die("EDL entry too long");
    size_t need = e->len + (size_t)w + 2;
    if (need > (size_t)e->cap) {
        while (e->cap < need) e->cap *= 2;
        e->buf = xrealloc(e->buf, (size_t)e->cap);
    }
    if (e->count > 0) e->buf[e->len++] = ',';
    memcpy(e->buf + e->len, entry, (size_t)w + 1);
    e->len += (size_t)w;
    e->count++;
}

/* ------------------------------------------------------------------ */
/* rng engine (faithful michacka port)                                 */

static int pick_slice(Track *t, double want_span, double *in_sec, double *span)
{
    if (t->dur == 0.0) track_probe_duration(t);
    double dur = t->dur > 0.0 ? t->dur : 0.0;
    if (dur <= 0.0) {
        *span = clampd(want_span, 3.0, 90.0);
        *in_sec = rnd_range(0.0, 30.0);
        return 1;
    }
    double s = want_span;
    if (s > dur * 0.9) s = dur * 0.9;
    if (s < 3.0) return 0;
    *span = s;
    *in_sec = dur > s ? rnd_range(0.0, dur - s) : 0.0;
    return 1;
}

static int add_layered_entries(Edl *edl, TrackList *lib, int role, int want,
                               double len, double span_lo, double span_hi,
                               double vol_lo, double vol_hi, double fin_lo, double fin_hi,
                               const char *gen, int *warned_no_role)
{
    if (want <= 0 || lib->n == 0) return 0;
    int *order = xmalloc(sizeof(int) * (size_t)lib->n);
    int avail = collect_roles(lib, role, order);
    if (avail == 0) {
        if (!*warned_no_role) {
            fprintf(stderr, "gram: warning: no %s-role source found, layer skipped\n",
                    role == ROLE_AMBIENT ? "ambient" : role == ROLE_MOTION ? "motion" : "pulse");
            *warned_no_role = 1;
        }
        free(order);
        return 0;
    }
    shuffle_ints(order, (size_t)avail);
    double max_sp = len - PLAN_MARGIN;
    double slo = span_lo, shi = span_hi;
    if (role != ROLE_AMBIENT) {
        if (shi > len * 0.45) shi = len * 0.45;
        if (slo > shi) slo = shi * 0.6;
    }
    int pos = 0;
    int added = 0;
    for (int k = 0; k < want; k++) {
        if (edl->count >= MAX_EDL_ENTRIES) break;
        if (pos >= avail) {
            if (avail >= want || pos == 0) break;
            pos = 0;
        }
        Track *t = &lib->v[order[pos]];
        pos++;
        double in_sec, sp;
        if (!pick_slice(t, rnd_range(slo, shi), &in_sec, &sp)) continue;
        if (sp > max_sp) {
            sp = max_sp;
            if (t->dur > 0.0 && in_sec > t->dur - sp) in_sec = t->dur > sp ? t->dur - sp : 0.0;
        }
        if (sp < 3.0) continue;
        double at = 0.0;
        if (role != ROLE_AMBIENT) {
            double slot = ((double)added + 0.5) * len / (double)want;
            at = clampd(slot + rnd_range(-0.07, 0.07) * len, 0.0, max_sp - sp);
            if (at < 0.0) at = 0.0;
        }
        double vol = rnd_range(vol_lo, vol_hi);
        double fin = rnd_range(fin_lo, fin_hi);
        double fout = rnd_range(fin_lo, fin_hi);
        edl_put(edl, "in%.1f out%.1f at%.1f v%.0f fin%.1f fout%.1f %s",
                in_sec, in_sec + sp, at, vol, fin, fout, t->path);
        edl_vedl(edl, gen, 0);
        added++;
    }
    free(order);
    return added;
}

static int add_field_entries(Edl *edl, TrackList *fld, int want, double len, const char *gen)
{
    if (want <= 0 || fld->n == 0) return 0;
    int *order = xmalloc(sizeof(int) * (size_t)fld->n);
    for (int i = 0; i < fld->n; i++) order[i] = i;
    shuffle_ints(order, (size_t)fld->n);
    double max_sp = len - PLAN_MARGIN;
    double slo = 90.0, shi = 300.0;
    if (shi > len * 0.5) shi = len * 0.5;
    if (slo > shi) slo = shi * 0.6;
    int added = 0;
    for (int k = 0; k < want && added < want; k++) {
        if (edl->count >= MAX_EDL_ENTRIES) break;
        Track *t = &fld->v[order[k % fld->n]];
        double in_sec, sp;
        if (!pick_slice(t, rnd_range(slo, shi), &in_sec, &sp)) continue;
        if (sp > max_sp) {
            sp = max_sp;
            if (t->dur > 0.0 && in_sec > t->dur - sp) in_sec = t->dur > sp ? t->dur - sp : 0.0;
        }
        if (sp < 3.0) continue;
        double slot = ((double)added + rnd_range(0.35, 0.65)) * len / (double)want;
        double at = clampd(slot, 0.0, max_sp - sp);
        if (at < 0.0) at = 0.0;
        double vol = rnd_range(-8.0, -2.0);
        double fin = rnd_range(6.0, 20.0);
        double fout = rnd_range(6.0, 20.0);
        edl_put(edl, "in%.1f out%.1f at%.1f v%.0f fin%.1f fout%.1f %s",
                in_sec, in_sec + sp, at, vol, fin, fout, t->path);
        edl_vedl(edl, gen, 0);
        added++;
    }
    free(order);
    return added;
}

static void build_arc(char *buf, size_t cap, const StyleSpec *st, double len)
{
    size_t off = 0;
    int K = 5;
    buf[0] = 0;
    for (int i = 0; i < K; i++) {
        float t = (float)i / (float)(K - 1);
        float g = env_eval(st->gain, st->ng, t) + (float)rnd_range(-0.04, 0.04);
        g = clampd(g, 0.05, 1.0);
        int w = snprintf(buf + off, cap - off, "%s%.0f:%.2f", i ? "," : "", t * len, g);
        if (w < 0 || (size_t)w >= cap - off) die("arc overflow");
        off += (size_t)w;
    }
}

/* ------------------------------------------------------------------ */
/* omicron engine: enumerated operator structures as the score         */

#define PHI 0.6180339887498949

typedef struct {
    const OmicronExpr *e;
    int n_ops;
    double acc[OMICRON_MAX_LETTERS]; /* acc[i] after applying op i */
    int slot;                        /* global step counter across all layers */
} Score;

static void score_init(Score *sc, const OmicronExpr *e)
{
    sc->e = e;
    sc->n_ops = e->n_ops;
    sc->slot = 0;
    double acc = 1.0;
    for (int i = 0; i < e->n_ops; i++) {
        acc = omicron_apply_op(e->op[i], acc, (double)(i + 2));
        sc->acc[i] = acc;
    }
}

static void score_step(Score *sc, int *op, double *acc, int *letter)
{
    int i = sc->slot % sc->n_ops;
    sc->slot++;
    *op = sc->e->op[i];
    *acc = sc->acc[i];
    *letter = i + 2;
}

/* deterministic pool ordering: sort track indices by path */
static void sort_pool(const TrackList *lib, int *pool, int n)
{
    /* insertion sort by path — pools are small, keeps qsort_r away */
    for (int i = 1; i < n; i++) {
        int key = pool[i];
        int j = i - 1;
        while (j >= 0 && strcmp(lib->v[pool[j]].path, lib->v[key].path) > 0) {
            pool[j + 1] = pool[j];
            j--;
        }
        pool[j + 1] = key;
    }
}

/* bounded structural map of the accumulator: (-1,1) */
static double squish(double acc, double scale)
{
    return tanh(acc / scale);
}

static int omicron_add_layered(Edl *edl, TrackList *lib, int role, int want,
                               double len, double span_lo, double span_hi,
                               double vol_lo, double vol_hi, double fin_lo, double fin_hi,
                               Score *sc, int *warned_no_role)
{
    if (want <= 0 || lib->n == 0) return 0;
    int *pool = xmalloc(sizeof(int) * (size_t)lib->n);
    int avail = collect_roles(lib, role, pool);
    if (avail == 0) {
        if (!*warned_no_role) {
            fprintf(stderr, "gram: warning: no %s-role source found, layer skipped\n",
                    role == ROLE_AMBIENT ? "ambient" : role == ROLE_MOTION ? "motion" : "pulse");
            *warned_no_role = 1;
        }
        free(pool);
        return 0;
    }
    sort_pool(lib, pool, avail);
    double max_sp = len - PLAN_MARGIN;
    double slo = span_lo, shi = span_hi;
    if (role != ROLE_AMBIENT) {
        if (shi > len * 0.45) shi = len * 0.45;
        if (slo > shi) slo = shi * 0.6;
    }
    int added = 0;
    for (int k = 0; k < want; k++) {
        if (edl->count >= MAX_EDL_ENTRIES) break;
        int op, letter;
        double acc;
        score_step(sc, &op, &acc, &letter);

        Track *t = &lib->v[pool[(letter - 2) % avail]];

        double frac = fmod(fabs(acc) * PHI, 1.0);
        double sp = slo + (shi - slo) * frac;

        if (t->dur == 0.0) track_probe_duration(t);
        double dur = t->dur > 0.0 ? t->dur : 30.0;
        if (sp > dur * 0.9) sp = dur * 0.9;
        if (sp > max_sp) sp = max_sp;
        if (sp < 3.0) continue;
        double in_sec = fmod(fabs(acc) * PHI * 7.0, 1.0) * (dur - sp);
        if (in_sec < 0.0) in_sec = 0.0;

        double vol = vol_lo + (vol_hi - vol_lo) * (0.5 + 0.5 * squish(acc, 24.0));
        double fin = fin_lo + (fin_hi - fin_lo) * frac;
        double fout = fin;
        switch (op) {
        case 1: fin = fout = 1.0; break;                 /* -: hard cut */
        case 2: fout *= 1.6; break;                      /* x: swell-out */
        case 3: fin = fout = fin > 3.0 ? 3.0 : fin; break; /* /: gate */
        default: break;                                  /* +: role default */
        }

        double at = 0.0;
        if (role != ROLE_AMBIENT) {
            double slot = ((double)added + 0.5) * len / (double)want;
            at = clampd(slot + 0.05 * len * squish(acc, 13.0), 0.0, max_sp - sp);
            if (at < 0.0) at = 0.0;
        }

        edl_put(edl, "in%.1f out%.1f at%.1f v%.0f fin%.1f fout%.1f %s",
                in_sec, in_sec + sp, at, vol, fin, fout, t->path);
        const char *gen = (op == 0 || op == 2) ? "file" : (op == 1 ? "wave" : "scope");
        if (role == ROLE_AMBIENT)
            gen = (op == 0) ? "wave" : "scope";
        edl_vedl(edl, gen, op);
        added++;
    }
    free(pool);
    return added;
}

static int omicron_add_fields(Edl *edl, TrackList *fld, int want, double len, Score *sc)
{
    if (want <= 0 || fld->n == 0) return 0;
    int *pool = xmalloc(sizeof(int) * (size_t)fld->n);
    for (int i = 0; i < fld->n; i++) pool[i] = i;
    sort_pool(fld, pool, fld->n);
    double max_sp = len - PLAN_MARGIN;
    double slo = 90.0, shi = 300.0;
    if (shi > len * 0.5) shi = len * 0.5;
    if (slo > shi) slo = shi * 0.6;
    int added = 0;
    for (int k = 0; k < want; k++) {
        if (edl->count >= MAX_EDL_ENTRIES) break;
        int op, letter;
        double acc;
        score_step(sc, &op, &acc, &letter);

        Track *t = &fld->v[pool[(letter - 2) % fld->n]];

        double frac = fmod(fabs(acc) * PHI, 1.0);
        double sp = slo + (shi - slo) * frac;
        if (t->dur == 0.0) track_probe_duration(t);
        double dur = t->dur > 0.0 ? t->dur : 60.0;
        if (sp > dur * 0.9) sp = dur * 0.9;
        if (sp > max_sp) sp = max_sp;
        if (sp < 3.0) continue;
        double in_sec = fmod(fabs(acc) * PHI * 11.0, 1.0) * (dur - sp);
        if (in_sec < 0.0) in_sec = 0.0;

        double slot = ((double)added + 0.5) * len / (double)want;
        double at = clampd(slot, 0.0, max_sp - sp);
        if (at < 0.0) at = 0.0;
        double vol = -8.0 + 6.0 * (0.5 + 0.5 * squish(acc, 24.0));
        double fin = 6.0 + 14.0 * frac;
        double fout = fin;
        edl_put(edl, "in%.1f out%.1f at%.1f v%.0f fin%.1f fout%.1f %s",
                in_sec, in_sec + sp, at, vol, fin, fout, t->path);
        edl_vedl(edl, "file", op);
        added++;
    }
    free(pool);
    return added;
}

static void build_arc_omicron(char *buf, size_t cap, const StyleSpec *st, double len, Score *sc)
{
    size_t off = 0;
    int K = 5;
    buf[0] = 0;
    for (int i = 0; i < K; i++) {
        float t = (float)i / (float)(K - 1);
        float g_env = env_eval(st->gain, st->ng, t);
        double a = sc->acc[(i * sc->n_ops / K) % sc->n_ops];
        float g = g_env * (float)(0.9 + 0.25 * squish(a, 24.0));
        g = clampd(g, 0.05, 1.0);
        int w = snprintf(buf + off, cap - off, "%s%.0f:%.2f", i ? "," : "", t * len, g);
        if (w < 0 || (size_t)w >= cap - off) die("arc overflow");
        off += (size_t)w;
    }
}

/* spread expression selection over the enumeration space: two passes,
 * first counts the space, second picks evenly spaced indices */
typedef struct {
    long long total;
    long long next_pick;
    long long idx;
    OmicronExpr *out;
    long long got, want;
} SpreadCtx;

static void spread_count_cb(const OmicronExpr *e, void *ud)
{
    (void)e;
    ((SpreadCtx *)ud)->total++;
}

static void spread_pick_cb(const OmicronExpr *e, void *ud)
{
    SpreadCtx *c = ud;
    if (c->got >= c->want) return;
    if (c->idx == c->next_pick) {
        c->out[c->got++] = *e;
        c->next_pick = (c->got * c->total) / c->want;
    }
    c->idx++;
}

static long long omicron_collect_spread(int n_letters, long long want, OmicronExpr *out)
{
    SpreadCtx c;
    memset(&c, 0, sizeof(c));
    omicron_run(n_letters, 0, 0.0, 1, spread_count_cb, &c);
    if (c.total == 0) return 0;
    if (c.total <= want) {
        return omicron_collect(n_letters, 0, 0.0, 1, want, out);
    }
    memset(&c, 0, sizeof(c));
    c.out = out;
    c.want = want;
    c.next_pick = 0;
    omicron_run(n_letters, 0, 0.0, 1, spread_pick_cb, &c);
    return c.got;
}

/* ------------------------------------------------------------------ */
/* planning drivers                                                    */

void plan_run(const PlanCfg *cfg, TrackList *mus, TrackList *fld, PlanResult *out)
{
    const StyleSpec *st = plan_style(cfg->style);
    double part_len = cfg->part_len;

    memset(out, 0, sizeof(*out));
    out->music_edls = xmalloc(sizeof(char *) * (size_t)cfg->parts);
    out->field_edls = xmalloc(sizeof(char *) * (size_t)cfg->parts);
    out->arcs = xmalloc(sizeof(char *) * (size_t)cfg->parts);
    out->vedls = xmalloc(sizeof(char *) * (size_t)cfg->parts);

    OmicronExpr *exprs = NULL;
    Score score;
    if (cfg->engine == PLAN_ENGINE_OMICRON) {
        exprs = xmalloc(sizeof(OmicronExpr) * (size_t)cfg->parts);
        long long got;
        if (cfg->has_target)
            got = omicron_collect(cfg->omicron_letters, 1, cfg->target,
                                  cfg->parts, cfg->parts, exprs);
        else
            got = omicron_collect_spread(cfg->omicron_letters, cfg->parts, exprs);
        if (got <= 0)
            die("omicron engine: no expressions found (letters=%d%s)",
                cfg->omicron_letters,
                cfg->has_target ? ", target unreachable" : "");
        printf("structure: %d omicron expression(s) over letters a..%c (%s)\n",
               (int)got, 'a' + cfg->omicron_letters - 1,
               cfg->has_target ? "reverse-search" : "spread enumeration");
    } else {
        rng_seed(cfg->seed);
    }

    int warned_bed = 0, warned_motion = 0, warned_pulse = 0;

    printf("planning movements:\n");
    for (int p = 0; p < cfg->parts; p++) {
        float phase = cfg->parts > 1 ? (float)p / (float)(cfg->parts - 1) : 0.0f;

        if (cfg->engine == PLAN_ENGINE_OMICRON)
            score_init(&score, &exprs[p % cfg->parts]);

        Edl m;
        edl_init(&m, 1);
        int nb = plan_layer_count(st, st->beds, st->nb, phase, p);
        int nm = plan_layer_count(st, st->motion, st->nm, phase, p);
        int npp = plan_layer_count(st, st->pulses, st->np, phase, p);
        int nf = plan_layer_count(st, st->fields, st->nfl, phase, p);

        int budget = MAX_EDL_ENTRIES - 2;
        if (nb > budget) nb = budget;
        if (cfg->engine == PLAN_ENGINE_RNG) {
            add_layered_entries(&m, mus, ROLE_AMBIENT, nb, part_len,
                                part_len * 0.5, part_len * 0.75,
                                -14.0, -10.0, st->fade_in[0], st->fade_out[1],
                                "wave", &warned_bed);
            budget -= m.count;
            int nm2 = nm > budget ? budget : nm;
            add_layered_entries(&m, mus, ROLE_MOTION, nm2, part_len,
                                120.0, 260.0, -8.0, -4.0, 6.0, 12.0, "file", &warned_motion);
            budget -= m.count;
            int npp2 = npp > budget ? budget : npp;
            add_layered_entries(&m, mus, ROLE_PULSE, npp2, part_len,
                                50.0, 150.0, -7.0, -4.0, 4.0, 8.0, "scope", &warned_pulse);
        } else {
            omicron_add_layered(&m, mus, ROLE_AMBIENT, nb, part_len,
                                part_len * 0.5, part_len * 0.75,
                                -14.0, -10.0, st->fade_in[0], st->fade_out[1],
                                &score, &warned_bed);
            omicron_add_layered(&m, mus, ROLE_MOTION, nm, part_len,
                                120.0, 260.0, -8.0, -4.0, 6.0, 12.0,
                                &score, &warned_motion);
            omicron_add_layered(&m, mus, ROLE_PULSE, npp, part_len,
                                50.0, 150.0, -7.0, -4.0, 4.0, 8.0,
                                &score, &warned_pulse);
        }

        Edl f;
        edl_init(&f, 1);
        if (cfg->engine == PLAN_ENGINE_RNG)
            add_field_entries(&f, fld, nf, part_len, "file");
        else
            omicron_add_fields(&f, fld, nf, part_len, &score);

        char arc[512];
        if (cfg->engine == PLAN_ENGINE_RNG)
            build_arc(arc, sizeof(arc), st, part_len);
        else
            build_arc_omicron(arc, sizeof(arc), st, part_len, &score);

        out->music_edls[p] = xstrdup(m.buf);
        out->field_edls[p] = xstrdup(f.buf);
        out->arcs[p] = xstrdup(arc);
        out->vedls[p] = m.vedl ? xstrdup(m.vedl) : NULL;

        printf("  part %02d: phase %.2f | music %d (bed%d/motion%d/pulse%d) | field %d | arc %s\n",
               p + 1, phase, m.count, nb, nm, npp, f.count, arc);

        edl_free(&m);
        edl_free(&f);
    }
    free(exprs);
}

void plan_free(const PlanCfg *cfg, PlanResult *out)
{
    for (int p = 0; p < cfg->parts; p++) {
        free(out->music_edls[p]);
        free(out->field_edls[p]);
        free(out->arcs[p]);
        free(out->vedls[p]);
    }
    free(out->music_edls);
    free(out->field_edls);
    free(out->arcs);
    free(out->vedls);
}
