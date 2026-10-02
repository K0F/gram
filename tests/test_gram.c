#include "analysis.h"
#include "edit.h"
#include "library.h"
#include "omicron.h"
#include "partition.h"
#include "plan.h"
#include "render.h"
#include "title.h"
#include "util.h"
#include "visual.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static void test_rng_determinism(void)
{
    double a[4], b[4];
    rng_seed(777);
    for (int i = 0; i < 4; i++) a[i] = rnd_unit();
    rng_seed(777);
    for (int i = 0; i < 4; i++) b[i] = rnd_unit();
    CHECK(memcmp(a, b, sizeof(a)) == 0);
    for (int i = 0; i < 4; i++) CHECK(a[i] >= 0.0 && a[i] < 1.0);

    int order[64];
    for (int i = 0; i < 64; i++) order[i] = i;
    rng_seed(1);
    shuffle_ints(order, 64);
    for (int i = 0; i < 64; i++) CHECK(order[i] >= 0 && order[i] < 64);
    int seen[64];
    memset(seen, 0, sizeof(seen));
    for (int i = 0; i < 64; i++) seen[order[i]]++;
    for (int i = 0; i < 64; i++) CHECK(seen[i] == 1);
}

static void test_fnv(void)
{
    /* reference FNV-1a 64 */
    const char *s = "hello world";
    unsigned long h = 14695981039346656037UL;
    for (const char *p = s; *p; p++) {
        h ^= (unsigned char)*p;
        h *= 1099511628211UL;
    }
    CHECK(fnv1a(s) == h);
    CHECK(fnv1a("") == 14695981039346656037UL);
}

static void test_omicron(void)
{
    /* enumeration over a..c : 2 gaps x 4 ops = 16 expressions */
    OmicronExpr buf[64];
    long long n = omicron_collect(3, 0, 0.0, 1000, 64, buf);
    CHECK(n == 16);
    for (int i = 0; i < n; i++)
        CHECK(buf[i].n_ops == 2 && buf[i].op[0] <= 3 && buf[i].op[1] <= 3);

    /* values are unique across the 16 combos */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            if (buf[i].op[0] == buf[j].op[0] && buf[i].op[1] == buf[j].op[1])
                CHECK(0);
        }

    /* manual evaluation of one known combo: a - b + c = 1 - 2 + 3 = 2 */
    OmicronExpr e;
    e.n_ops = 2;
    e.op[0] = 1; /* minus */
    e.op[1] = 0; /* plus */
    e.value = omicron_apply_op(0, omicron_apply_op(1, 1.0, 2.0), 3.0);
    CHECK(fabs(e.value - 2.0) < 1e-12);
    char line[256];
    omicron_format(&e, line, sizeof(line));
    CHECK(strstr(line, "a") != NULL && strstr(line, "= 2") != NULL);

    /* reverse search finds the target */
    OmicronExpr hit[8];
    long long got = omicron_collect(6, 1, 42.0, 8, 8, hit);
    CHECK(got > 0);
    if (got > 0) CHECK(fabs(hit[0].value - 42.0) < 1e-9);
}

static void eval_expr(const OmicronExpr *e, double *out)
{
    double acc = 1.0;
    for (int i = 0; i < e->n_ops; i++)
        acc = omicron_apply_op(e->op[i], acc, (double)(i + 2));
    *out = acc;
}

static void test_camelot(void)
{
    /* camelot_pitch maps a Camelot label to a semitone pitch class */
    CHECK(camelot_pitch("8A") == ((11 - 7 * 5) % 12 + 12) % 12);
    CHECK(camelot_pitch("8B") == ((7 * 7) % 12));
    CHECK(camelot_pitch("?") == -1);
    CHECK(camelot_pitch("garbage") == -1);
    CHECK(camelot_dist("8A", "8A") == 0);
    CHECK(camelot_dist("1A", "1B") == camelot_dist("1B", "1A"));
    int d = camelot_dist("1A", "12A");
    CHECK(d >= 0 && d <= 6);
}

static void test_arc_and_spec(void)
{
    ArcPoint arc[MAX_ARC];
    int n = render_parse_arc_str("0:0.5,10:1.0,20:0.25", arc, MAX_ARC);
    CHECK(n == 3);
    CHECK(arc[0].t == 0.0f && arc[0].g == 0.5f);
    CHECK(arc[2].t == 20.0f && fabs(arc[2].g - 0.25f) < 1e-6);

    TrackSpec sp;
    memset(&sp, 0, sizeof(sp));
    parse_track_spec("in1.5 out4 at2 v-6 fin0.5 fout1 /tmp/x.wav", &sp);
    CHECK(sp.has_in && fabs(sp.in_sec - 1.5f) < 1e-5);
    CHECK(sp.has_out && fabs(sp.out_sec - 4.0f) < 1e-5);
    CHECK(sp.has_at && fabs(sp.at_sec - 2.0f) < 1e-5);
    CHECK(sp.has_fin && sp.fin_sec == 0.5f);
    CHECK(sp.has_fout && sp.fout_sec == 1.0f);
    /* v-6 dB is stored as a linear gain (tj semantics) */
    CHECK(fabs(sp.vol - powf(10.0f, -6.0f / 20.0f)) < 1e-5);
    CHECK(strcmp(sp.filepath, "/tmp/x.wav") == 0);
}

static void test_texture_label(void)
{
    CHECK(strcmp(texture_label(-1.0f), "n/a") == 0);
    CHECK(strcmp(texture_label(0.10f), "ambient") == 0);
    CHECK(strcmp(texture_label(0.40f), "motion") == 0);
    CHECK(strcmp(texture_label(0.90f), "pulse") == 0);
}

static void test_env(void)
{
    EnvPt env[] = { { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 2.0f, 0.0f } };
    CHECK(env_eval(env, 3, 0.5f) == 0.5f);
    CHECK(env_eval(env, 3, 1.5f) == 0.5f);
    CHECK(env_eval(env, 3, -5.0f) == 0.0f);
    CHECK(env_eval(env, 3, 9.0f) == 0.0f);
}

static void test_frame_blend(void)
{
    Frame *m = frame_new(8, 8), *s = frame_new(8, 8);
    frame_clear(m, 10, 20, 30);
    frame_clear(s, 200, 200, 200);
    frame_blend(m, s, '+', 1.0f);
    CHECK(m->px[0] == 210 && m->px[1] == 220 && m->px[2] == 230);
    frame_clear(m, 10, 20, 30);
    frame_blend(m, s, '-', 1.0f);
    CHECK(m->px[0] == 190 && m->px[1] == 180 && m->px[2] == 170);
    frame_clear(m, 100, 100, 100);
    frame_blend(m, s, 'x', 1.0f);
    CHECK(m->px[0] == 78); /* 100*200/255 */
    free(m->px); free(m);
    free(s->px); free(s);
}

static void test_edit_plan(void)
{
    const char *paths[3] = { "/v/a.mp4", "/v/b.mp4", "/v/c.mp4" };
    double durs[3] = { 60.0, 60.0, 60.0 };
    EditCfg c;
    edit_cfg_defaults(&c);
    CHECK(fabs(c.span - 0.432) < 1e-9);

    EditCut cuts[64], again[64];
    memset(cuts, 0, sizeof(cuts));
    memset(again, 0, sizeof(again));
    long n = edit_plan_text("this is source string", paths, durs, 3, &c, cuts, 64);
    CHECK(n == 18); /* letters only */

    /* letter -> (v-1) mod pool: 't' = 20 -> (20-1)%3 = 1 */
    CHECK(cuts[0].clip == ('t' - 'a') % 3);
    CHECK(fabs(cuts[0].at) < 1e-9);
    CHECK(fabs(cuts[0].span - c.span) < 1e-9);
    /* k=0: golden frac is 0, cut starts at clip head */
    CHECK(fabs(cuts[0].in_sec) < 1e-9);

    /* determinism: same text -> byte-identical plan */
    long n2 = edit_plan_text("this is source string", paths, durs, 3, &c,
                             again, 64);
    CHECK(n2 == 18);
    CHECK(memcmp(again, cuts, sizeof(EditCut) * 18) == 0);

    /* in-point follows text position via golden-ratio scatter */
    double phi = 0.6180339887498949;
    int k = 5;
    double want_in = fmod((double)k * phi, 1.0) * (60.0 - c.span);
    CHECK(fabs(cuts[k].in_sec - want_in) < 1e-6);

    /* timeline is continuous back-to-back: "a b" -> b right after a */
    n = edit_plan_text("a b", paths, durs, 3, &c, cuts, 64);
    CHECK(n == 2);
    CHECK(fabs(cuts[1].at - c.span) < 1e-9);

    /* uppercase folds to the same cut as lowercase */
    EditCut up[4], lo[4];
    CHECK(edit_plan_text("A", paths, durs, 3, &c, up, 4) == 1);
    CHECK(edit_plan_text("a", paths, durs, 3, &c, lo, 4) == 1);
    CHECK(up[0].clip == lo[0].clip && fabs(up[0].in_sec - lo[0].in_sec) < 1e-12);

    /* non-letters emit nothing */
    CHECK(edit_plan_text("123 .,", paths, durs, 3, &c, cuts, 64) == 0);

    /* unknown durations keep the configured span */
    double unk[1] = { 0.0 };
    const char *one[1] = { "/v/x.mp4" };
    CHECK(edit_plan_text("z", one, unk, 1, &c, cuts, 64) == 1);
    CHECK(fabs(cuts[0].span - c.span) < 1e-9);

    /* short clips clamp the span to 90% duration */
    double short_dur[1] = { 0.45 };
    CHECK(edit_plan_text("z", one, short_dur, 1, &c, cuts, 64) == 1);
    CHECK(fabs(cuts[0].span - 0.405) < 1e-9);

    /* overflow guard */
    CHECK(edit_plan_text("aaaa", paths, durs, 3, &c, cuts, 2) == -1);

    /* long texts plan fully: 1000 letters land back-to-back */
    static char big[1001];
    for (int i = 0; i < 1000; i++) big[i] = (char)('a' + i % 26);
    big[1000] = 0;
    static EditCut bigcuts[1000];
    memset(bigcuts, 0, sizeof(bigcuts));
    n = edit_plan_text(big, paths, durs, 3, &c, bigcuts, 1000);
    CHECK(n == 1000);
    CHECK(fabs(bigcuts[999].at - 999 * c.span) < 1e-6);
    CHECK(fabs(bigcuts[999].in_sec -
              fmod(999.0 * phi, 1.0) * (60.0 - c.span)) < 1e-6);
}

static void test_partition_lex(void)
{
    PTok toks[32];

    int n = part_lex("pf 7 x 3", toks, 32);
    CHECK(n == 5);                                  /* pf 7 x 3 EOF */
    CHECK(toks[0].type == PT_VERB && strcmp(toks[0].word, "pf") == 0);
    CHECK(toks[1].type == PT_NUM && toks[1].num == 7);
    CHECK(toks[2].type == PT_X);
    CHECK(toks[3].type == PT_NUM && toks[3].num == 3);
    CHECK(toks[4].type == PT_EOF);

    n = part_lex("H 4 2", toks, 32);
    CHECK(n == 4);
    CHECK(toks[0].type == PT_KW && strcmp(toks[0].word, "H") == 0);
    CHECK(toks[1].type == PT_NUM && toks[1].num == 4);
    CHECK(toks[2].type == PT_NUM && toks[2].num == 2);

    /* comments drop to end of line; newline is a word boundary */
    n = part_lex("pf 4 # hi\njf 2", toks, 32);
    CHECK(n == 5);                                  /* pf 4 jf 2 EOF */
    CHECK(strcmp(toks[2].word, "jf") == 0);
    CHECK(toks[3].num == 2);

    /* letter/digit mixed boundaries split words: "pf7" = pf, 7 */
    n = part_lex("pf7", toks, 32);
    CHECK(n == 3);
    CHECK(toks[0].type == PT_VERB && strcmp(toks[0].word, "pf") == 0);
    CHECK(toks[1].type == PT_NUM && toks[1].num == 7);

    /* unknown words become ERR tokens, not rejects */
    n = part_lex("zzz 9", toks, 32);
    CHECK(n == 3);
    CHECK(toks[0].type == PT_ERR && strcmp(toks[0].word, "zzz") == 0);
    CHECK(toks[1].type == PT_NUM && toks[1].num == 9);

    /* bare x is a repeat token */
    n = part_lex("x", toks, 32);
    CHECK(n == 2 && toks[0].type == PT_X);

    /* token capacity overflow returns -1 (incl. EOF slot) */
    CHECK(part_lex("pf 1 x 3", toks, 4) == -1);
}

static void test_partition_interp(void)
{
    PSeg seg[64];
    long long lens[2] = { 50, 200 };
    long long full[2] = { 50, 200 };
    long long offs[2] = { 0, 50 };
    long long l200[1] = { 200 }, f200[1] = { 200 }, o0[1] = { 0 };
    PTok toks[32];

    /* single virtual source, pattern mode */
    {
        CHECK(part_lex("pf 2 x 3", toks, 32) > 0);
        int n = part_interp(toks, 5, lens, full, offs, 1, 0, 10, seg, 64);
        CHECK(n == 3);
        CHECK(seg[0].b == 0 && seg[0].e == 20);
        CHECK(seg[1].b == 20 && seg[1].e == 40);
        CHECK(seg[2].b == 40 && seg[2].e == 50);    /* clamped at end */
    }

    /* jf advances the playhead before pf emits */
    {
        CHECK(part_lex("jf 5 pf 3", toks, 32) > 0);
        int n = part_interp(toks, 5, l200, f200, o0, 1, 0, 10, seg, 64);
        CHECK(n == 1);
        CHECK(seg[0].b == 50 && seg[0].e == 80);
    }

    /* jb clamps at the source start */
    {
        CHECK(part_lex("jf 2 jb 9 pf 99", toks, 32) > 0);
        int n = part_interp(toks, 7, l200, f200, o0, 1, 0, 10, seg, 64);
        CHECK(n == 1);
        CHECK(seg[0].b == 0 && seg[0].e == 200);
    }

    /* script mode: open switches sources, combined offsets track them */
    {
        CHECK(part_lex("open 1 pf 2", toks, 32) > 0);
        int n = part_interp(toks, 5, lens, full, offs, 2, 1, 10, seg, 64);
        CHECK(n == 1);
        CHECK(seg[0].b == 50 && seg[0].e == 70);
    }

    /* back-and-forth across two opened sources */
    {
        CHECK(part_lex("open 0 pf 1 open 1 pf 1", toks, 32) > 0);
        int n = part_interp(toks, 9, lens, full, offs, 2, 1, 10, seg, 64);
        CHECK(n == 2);
        CHECK(seg[0].b == 0 && seg[0].e == 10);
        CHECK(seg[1].b == 50 && seg[1].e == 60);
    }

    /* out-of-range open and missing-first-open are errors */
    {
        CHECK(part_lex("open 7 pf 1", toks, 32) > 0);
        CHECK(part_interp(toks, 5, lens, full, offs, 2, 1, 10, seg, 64) == -1);
        CHECK(part_lex("pf 1", toks, 32) > 0);
        CHECK(part_interp(toks, 3, lens, full, offs, 2, 1, 10, seg, 64) == -1);
    }

    /* H harmonic crosscut: nsec x play = 8 one-unit cuts inside [0,200) */
    {
        CHECK(part_lex("H 4 2", toks, 32) > 0);
        int n = part_interp(toks, 4, l200, f200, o0, 1, 0, 10, seg, 64);
        CHECK(n == 8);
        for (int i = 0; i < n; i++) {
            CHECK(seg[i].b >= 0 && seg[i].e <= 200 && seg[i].b < seg[i].e);
            if (i > 0) CHECK(seg[i].b >= seg[i - 1].b);
        }
    }
}

static void test_title_fit(void)
{
    int s = title_fit_size(932, 782.88f);           /* 100 * 0.84*932 / target */
    CHECK(s == 100);
    s = title_fit_size(932, 0.001f);                /* huge -> clamp high */
    CHECK(s == 160);
    s = title_fit_size(932, 1e9f);                  /* tiny -> clamp low */
    CHECK(s == 24);
    s = title_fit_size(64, 2000.0f);                /* 64px raster stays >= 24 */
    CHECK(s >= 24 && s <= 160);
}

int main(void)
{
    test_rng_determinism();
    test_fnv();
    test_omicron();
    test_camelot();
    test_arc_and_spec();
    test_texture_label();
    test_env();
    test_frame_blend();
    test_edit_plan();
    test_partition_lex();
    test_partition_interp();
    test_title_fit();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
