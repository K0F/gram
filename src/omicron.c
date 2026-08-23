#include "omicron.h"

#include "util.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define ABS_SLACK 1e-9
#define REL_SLACK 1e-11

static const char *ops_utf8[OMICRON_N_OPS] = { "+", "\u2212", "\u00d7", "\u00f7" };

double omicron_apply_op(int op, double x, double v)
{
    switch (op) {
    case 0: return x + v;
    case 1: return x - v;
    case 2: return x * v;
    default: return x / v;
    }
}

char omicron_op_char(int op)
{
    static const char chars[OMICRON_N_OPS] = { '+', '-', '*', '/' };
    return (op >= 0 && op < OMICRON_N_OPS) ? chars[op] : '?';
}

const char *omicron_op_utf8(int op)
{
    return (op >= 0 && op < OMICRON_N_OPS) ? ops_utf8[op] : "?";
}

typedef struct {
    int n_letters;
    int has_target;
    double target;
    long long limit;
    OmicronEmit emit;
    void *ud;

    double bnd_lo[OMICRON_MAX_LETTERS + 1];
    double bnd_hi[OMICRON_MAX_LETTERS + 1];

    unsigned char cur_ops[OMICRON_MAX_LETTERS];
    long long emitted;
    int stop;
} Ctx;

static void build_bounds(Ctx *c)
{
    double lo = c->target - OMICRON_EPS;
    double hi = c->target + OMICRON_EPS;
    for (int g = c->n_letters; g >= 2; g--) {
        double v = g;
        double l[4] = { lo - v, lo + v, lo / v, lo * v };
        double h[4] = { hi - v, hi + v, hi / v, hi * v };
        double m = l[0], M = h[0];
        for (int k = 1; k < 4; k++) {
            if (l[k] < m) m = l[k];
            if (h[k] > M) M = h[k];
        }
        c->bnd_lo[g] = m - ABS_SLACK - REL_SLACK * (1.0 + fabs(m));
        c->bnd_hi[g] = M + ABS_SLACK + REL_SLACK * (1.0 + fabs(M));
        lo = c->bnd_lo[g];
        hi = c->bnd_hi[g];
    }
}

static void dfs(Ctx *c, int consumed, double acc)
{
    if (c->stop) return;
    if (consumed == c->n_letters) {
        if (!c->has_target || fabs(acc - c->target) < OMICRON_EPS) {
            OmicronExpr e;
            e.n_ops = c->n_letters - 1;
            memcpy(e.op, c->cur_ops, (size_t)e.n_ops);
            e.value = acc;
            c->emit(&e, c->ud);
            c->emitted++;
            if (c->has_target && c->emitted >= c->limit) c->stop = 1;
        }
        return;
    }
    int vnext = consumed + 1;
    for (int op = 0; op < OMICRON_N_OPS && !c->stop; op++) {
        double next = omicron_apply_op(op, acc, vnext);
        if (c->has_target && (next < c->bnd_lo[vnext] || next > c->bnd_hi[vnext]))
            continue;
        c->cur_ops[consumed - 1] = (unsigned char)op;
        dfs(c, consumed + 1, next);
    }
}

long long omicron_run(int n_letters, int has_target, double target,
                      long long limit, OmicronEmit emit, void *ud)
{
    if (n_letters < 1 || n_letters > OMICRON_MAX_LETTERS) return 0;
    if (limit < 1) limit = 1;
    Ctx c;
    memset(&c, 0, sizeof(c));
    c.n_letters = n_letters;
    c.has_target = has_target;
    c.target = target;
    c.limit = limit;
    c.emit = emit;
    c.ud = ud;
    if (has_target) build_bounds(&c);
    dfs(&c, 1, 1.0);
    return c.emitted;
}

typedef struct {
    OmicronExpr *out;
    long long want;
    long long got;
} Collector;

static void collect_emit(const OmicronExpr *e, void *ud)
{
    Collector *col = ud;
    if (col->got >= col->want) return;
    col->out[col->got++] = *e;
}

long long omicron_collect(int n_letters, int has_target, double target,
                          long long limit, long long want, OmicronExpr *out)
{
    Collector col = { out, want, 0 };
    /* note: enumeration cannot be stopped early in untargeted mode once the
     * collector is full; callers should use want values they can afford */
    omicron_run(n_letters, has_target, target, limit, collect_emit, &col);
    return col.got;
}

void omicron_format(const OmicronExpr *e, char *buf, size_t cap)
{
    size_t off = 0;
    int w = snprintf(buf, cap, "a");
    if (w < 0) { buf[0] = 0; return; }
    off = (size_t)w;
    for (int i = 0; i < e->n_ops; i++) {
        w = snprintf(buf + off, cap - off, " %s %c",
                     omicron_op_utf8(e->op[i]), 'a' + i + 1);
        if (w < 0 || (size_t)w >= cap - off) break;
        off += (size_t)w;
    }
    snprintf(buf + off, cap - off, " = %.7g", e->value);
}
