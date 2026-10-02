#include "analysis.h"
#include "av_render.h"
#include "compose.h"
#include "edit.h"
#include "flags.h"
#include "library.h"
#include "slides.h"
#include "omicron.h"
#include "partition.h"
#include "plan.h"
#include "render.h"
#include "title.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s <command> [args]\n"
        "\n"
        "structuralist avantgarde AV toolkit — tj EDL engine + michacka\n"
        "planning + OperatorOmikron structure, rendered to WAV and MP4.\n"
        "\n"
        "common flags (accepted by every command, ignored where not applicable):\n"
        "  --w N --h N --fps N   output raster\n"
        "  --max N               cap on files / images / expressions (omicron: = --limit)\n"
        "  --mute                silent video output (no audio mux)\n"
        "  --out PATH            output path or prefix (av/edit/slides/dada: the out.mp4)\n"
        "  --seed N              RNG seed\n"
        "  every value flag accepts both '--flag value' and '--flag=value'\n"
        "\n"
        "  omicron [-n N] [--reverse R] [--limit K] [--force]\n"
        "      enumerate operator expressions over letters a..z = 1..26\n"
        "  analyze <file>...\n"
        "      BPM / Camelot key / rhythmic texture (cached in ~/.cache/tj)\n"
        "  render \"<edl>\" [out.wav] [--bpm auto|N] [--snap] [--keylock auto|K]\n"
        "          [--fade-in S] [--fade-out S] [--arc t:g,...] [--master pop|subtle]\n"
        "      mix an EDL to a 48k stereo WAV (tj-compatible)\n"
        "  plan <style> [seed] [--parts N] [--len S] [--out PREFIX] [--dry-run]\n"
        "       [--engine rng|omicron] [--letters N] [--target R] [--max N] [--av]\n"
        "      plan movements, write .edl (+.vedl) sidecars only\n"
        "  av \"<edl>\" out.mp4 [--vedl F] [--arc t:g,...] [--vid DIR]\n"
        "     [--w W] [--h H] [--fps N]\n"
        "      render the visual edit of an EDL, muxed with <out>_audio.wav;\n"
        "      --vid falls back to $GRAM_VID, conf vid=, else an error\n"
        "  edit out.mp4 [--vid DIR] [--w W] [--h H] [--fps N]\n"
        "       [--span S] [--max N] [--edl FILE] [--mute]\n"
        "      text-driven video edit: stdin letters a..z pick clips\n"
        "      ((v-1) mod pool, path-sorted), their position in the text sets\n"
        "      the in-point (golden-ratio scatter); 0.432s slices, continuous,\n"
        "      muxed with the clips' original audio (--mute for silent)\n"
        "  slides out.mp4 --img DIR [--fld DIR] [--w W] [--h H] [--fps N]\n"
        "       [--dur S] [--seed N] [--max N] [--mute]\n"
        "       [--dada] [--title T] [--title-slots N] [--title-px P]\n"
        "       [--stone FILE] [--dada-bin PATH]\n"
        "      picture slideshow: crop/fill to format, grayscale, shuffled,\n"
        "      muxed with field recordings; 0.432s per slide by default.\n"
        "      --dada: deterministic stone-oracle film, exact 60s\n"
        "  dada out.mp4 [--img DIR] [--fld DIR] [--title T] [--title-slots N]\n"
        "       [--title-px P] [--stone FILE] [--dada-bin PATH] [--mute]\n"
        "      the committed dada film (60s, 932x576@25, 0.216s slides, kof26\n"
        "      title over the noise stone, B&W Camera pictures, dense field mix)\n"
        "      — byte-for-byte reproducible from the same inputs\n"
        "  compose <style> [seed] [--parts N] [--len S] [--out PREFIX] [--dry-run]\n"
        "          [--engine rng|omicron] [--letters N] [--target R] [--max N] [--av]\n"
        "      full pipeline: libraries -> plan -> render -> master -> mp4\n"
        "  title out.mp4 -t 'LINE' [-t ...] [-s PX] [-d SECS] [--stone FILE]\n"
        "       [--w W] [--h H] [--fps N] [--mute]\n"
        "      Gomotor title card: white text on black (or a greyscale --stone),\n"
        "      auto-fitted (or fixed -s px), muxed with a silent AAC bed\n"
        "      (video-only with --mute)\n"
        "  partition out.mp4 -p PATTERN | --script F [-i FILE ...] [--unit N]\n"
        "       [--std] [--edl F] [--mlt F] [--w W] [--h H] [--fps N] [--mute]\n"
        "      transport-DSL crosscut (open/jf/jb/pf/x/H) rendered silently\n"
        "      through the gram av engine; add --edl F / --mlt F to dump\n"
        "      mpv EDL v2 / MLT XML sidecars; --std normalizes sources\n"
        "\n"
        "styles: day | storm | drift | pulse | rupture | strata\n"
        "config: central ~/.config/gram.conf (mus= fld= vid= img=; michacka reads\n"
        "mus= fld= tj= from it too); path override env GRAM_CONF; per-key env\n"
        "GRAM_MUS / GRAM_FLD / GRAM_VID / GRAM_IMG; missing keys are errors.\n",
        prog);
}

/* ------------------------------------------------------------------ */
/* config                                                              */

typedef struct {
    char *mus, *fld, *vid, *img;
} GramConf;

static void conf_load(GramConf *c)
{
    char path[1024];
    const char *home = getenv("HOME");
    const char *ovr = getenv("GRAM_CONF");
    if (ovr && ovr[0])
        snprintf(path, sizeof(path), "%s", ovr);
    else {
        const char *xdg = getenv("XDG_CONFIG_HOME");
        if (xdg && xdg[0])
            snprintf(path, sizeof(path), "%s/gram.conf", xdg);
        else
            snprintf(path, sizeof(path), "%s/.config/gram.conf", home ? home : ".");
    }
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    char line[1200];
    while (fgets(line, sizeof(line), fp)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || *s == '\n' || *s == '\0') continue;
        char *eq = strchr(s, '=');
        if (!eq) die("%s: expected key=value", path);
        *eq = '\0';
        char *key = s, *val = eq + 1;
        char *e = key + strlen(key);
        while (e > key && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
        while (*val == ' ' || *val == '\t') val++;
        e = val + strlen(val);
        while (e > val && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
            *--e = '\0';
        char exp[1024];
        if (val[0] == '~' && (val[1] == '/' || val[1] == '\0'))
            snprintf(exp, sizeof(exp), "%s%s", home ? home : "", val + 1);
        else
            snprintf(exp, sizeof(exp), "%s", val);
        if (strcmp(key, "mus") == 0) c->mus = xstrdup(exp);
        else if (strcmp(key, "fld") == 0) c->fld = xstrdup(exp);
        else if (strcmp(key, "vid") == 0) c->vid = xstrdup(exp);
        else if (strcmp(key, "img") == 0) c->img = xstrdup(exp);
        else if (strcmp(key, "tj") == 0) { /* legacy key, engine is internal now */ }
        else die("%s: unknown key '%s' (expected mus|fld|vid|img|tj)", path, key);
    }
    fclose(fp);
}

/* ------------------------------------------------------------------ */
/* shared helpers                                                      */

static void avopts_from_core(AvOpts *o, const CoreFlags *c)
{
    if (c->w > 0) o->w = c->w;
    if (c->h > 0) o->h = c->h;
    if (c->fps > 0) o->fps = c->fps;
}

/* resolution order for config-driven dirs: explicit value > $GRAM_<KEY> >
 * central conf <key>= > die(). Death forces config on every machine. */
static const char *resolve_dir(const char *flag, const char *env_var, const char *conf_key)
{
    if (flag && flag[0]) return flag;
    const char *e = getenv(env_var);
    if (e && e[0]) return e;
    static GramConf conf = { 0 };
    static int loaded = 0;
    if (!loaded) { conf_load(&conf); loaded = 1; }
    const char *d = NULL;
    if (strcmp(conf_key, "mus") == 0) d = conf.mus;
    else if (strcmp(conf_key, "fld") == 0) d = conf.fld;
    else if (strcmp(conf_key, "vid") == 0) d = conf.vid;
    else if (strcmp(conf_key, "img") == 0) d = conf.img;
    if (d && d[0]) return d;
    die("no %s directory configured: set %s=/path in the central config\n"
        "  (~/.config/gram.conf, or point $GRAM_CONF at it)", conf_key, env_var);
    return NULL;
}

/* resolution order for the video pool: --vid > $GRAM_VID > conf vid= > error */
static const char *resolve_vid(void)
{
    return resolve_dir(NULL, "GRAM_VID", "vid");
}

static void free_pos(char **pos)
{
    free(pos);
}

/* ------------------------------------------------------------------ */
/* subcommands                                                         */

typedef struct {
    int n;
    double target;
    int has_target;
    long long limit;
    int force;
} OmicronArgs;

static int h_reverse(void *ctx, const char *name, const char *val)
{
    OmicronArgs *a = ctx;
    (void)name;
    if (!val) return -1;
    a->target = atof(val);
    a->has_target = 1;
    return 0;
}

static int cmd_omicron(int argc, char **argv)
{
    OmicronArgs a = { OMICRON_MAX_LETTERS, 0.0, 0, 1000, 0 };
    FlagSpec tbl[] = {
        { "-n",         FLAG_INT,  &a.n,      NULL,      0 },
        { "--reverse",  FLAG_CALL, NULL,      h_reverse, 0 },
        { "--limit",    FLAG_INT,  &a.limit,  NULL,      0 },
        { "--force",    FLAG_BOOL, &a.force,  NULL,      0 },
    };
    CoreFlags core;
    core_flags_defaults(&core);
    char **pos = NULL;
    int npos = 0;
    int r = flags_parse(argc, argv, 2, tbl, 4, &a, &core, &pos, &npos);
    free_pos(pos);
    if (r == -2) { usage(argv[0]); return 0; }
    if (r < 0) return 1;
    if (npos > 0) {
        fprintf(stderr, "usage: gram omicron [-n N] [--reverse R] [--limit K] [--force]\n");
        return 1;
    }
    if (a.n < 1 || a.n > OMICRON_MAX_LETTERS) {
        fprintf(stderr, "gram omicron: -n must be 1..%d\n", OMICRON_MAX_LETTERS);
        return 1;
    }
    if (core.max > 0) a.limit = core.max;   /* core --max is --limit's alias */

    unsigned long long combos = 1;
    for (int i = 2; i <= a.n; i++) combos *= 4;
    if (!a.has_target && combos > 1048576ULL && !a.force) {
        printf("a..%c: %d gaps x 4 operations = %llu possible expressions\n",
               'a' + a.n - 1, a.n - 1, combos);
        printf("too many to print; try:\n");
        printf("  gram omicron --reverse <number>   find expressions equal to it\n");
        printf("  gram omicron -n <N>              enumerate a smaller prefix\n");
        printf("  gram omicron -n <N> --force      print everything anyway\n");
        return 0;
    }

    long long count = 0;
    OmicronExpr *buf = xmalloc(sizeof(OmicronExpr) * 4096);
    for (;;) {
        long long want = a.has_target ? (a.limit - count < 4096 ? a.limit - count : 4096) : 4096;
        if (want <= 0) break;
        long long got = omicron_collect(a.n, a.has_target, a.target, want, want, buf);
        for (long long i = 0; i < got; i++) {
            char line[512];
            omicron_format(&buf[i], line, sizeof(line));
            printf("%s\n", line);
        }
        count += got;
        if (got < want || count >= a.limit) break;
        if (!a.has_target && count >= (long long)combos) break;
    }
    free(buf);
    if (a.has_target && count == 0)
        printf("no expression found for result %.7g\n", a.target);
    return 0;
}

static int cmd_analyze(int argc, char **argv)
{
    CoreFlags core;
    core_flags_defaults(&core);
    char **pos = NULL;
    int npos = 0;
    int r = flags_parse(argc, argv, 2, NULL, 0, NULL, &core, &pos, &npos);
    if (r == -2) { usage(argv[0]); free_pos(pos); return 0; }
    if (r < 0) { free_pos(pos); return 1; }
    if (npos < 1) {
        fprintf(stderr, "Usage: gram analyze <file>...\n");
        free_pos(pos);
        return 1;
    }
    for (int i = 0; i < npos; i++) {
        float bpm = 0.0f;
        char key[32] = "?";
        Texture tex;
        memset(&tex, 0, sizeof(tex));
        tex.rhythm = -1.0f;
        analyze_track(pos[i], &bpm, key, sizeof(key), &tex);
        printf("%-64s %6.1f BPM  %-4s  d=%.2f/s pulse=%.2f steady=%.2f  %s\n",
               pos[i], bpm, key, tex.density, tex.pulse, tex.steady,
               texture_label(tex.rhythm));
    }
    free_pos(pos);
    return 0;
}

static void write_simple(const char *path, const char *content)
{
    FILE *fp = fopen(path, "w");
    if (!fp) die("cannot write %s", path);
    fprintf(fp, "%s\n", content);
    fclose(fp);
}

static int h_engine(void *ctx, const char *name, const char *val)
{
    ComposeCfg *cc = ctx;
    (void)name;
    if (!val) return -1;
    if (strcmp(val, "rng") == 0) cc->plan.engine = PLAN_ENGINE_RNG;
    else if (strcmp(val, "omicron") == 0) cc->plan.engine = PLAN_ENGINE_OMICRON;
    else {
        fprintf(stderr, "gram: unknown engine '%s' (rng|omicron)\n", val);
        return -1;
    }
    return 0;
}

static int h_target(void *ctx, const char *name, const char *val)
{
    ComposeCfg *cc = ctx;
    (void)name;
    if (!val) return -1;
    cc->plan.has_target = 1;
    cc->plan.target = atof(val);
    return 0;
}

static void plan_prepare(PlanCfg *cfg, ComposeCfg *cc)
{
    if (!cfg->mus_dir) cfg->mus_dir = resolve_dir(NULL, "GRAM_MUS", "mus");
    if (!cfg->fld_dir) cfg->fld_dir = resolve_dir(NULL, "GRAM_FLD", "fld");
    if (!cc->vid_dir[0])
        snprintf(cc->vid_dir, sizeof(cc->vid_dir), "%s", resolve_dir(NULL, "GRAM_VID", "vid"));

    if (!cfg->out_prefix[0]) {
        const StyleSpec *st = plan_style(cfg->style);
        int mins = (int)((cfg->parts * cfg->part_len) / 60.0 + 0.5);
        snprintf(cfg->out_prefix, sizeof(cfg->out_prefix), "gram_%s_%dmin", st->name, mins);
    }
    if (!cfg->have_seed) {
        cfg->seed = (uint64_t)time(NULL) ^ ((uint64_t)getpid() << 32);
        if (cfg->seed == 0) cfg->seed = 1;
    }
}

static int cmd_plan_or_compose(int argc, char **argv, int do_compose)
{
    ComposeCfg cc;
    memset(&cc, 0, sizeof(cc));
    plan_cfg_defaults(&cc.plan);
    cc.plan.style = ST_DAY;
    cc.plan.engine = PLAN_ENGINE_RNG;

    FlagSpec tbl[] = {
        { "--parts",    FLAG_INT,   &cc.plan.parts,           NULL,      0 },
        { "--len",      FLAG_FLOAT, &cc.plan.part_len,        NULL,      0 },
        { "--engine",   FLAG_CALL,  NULL,                     h_engine,  0 },
        { "--letters",  FLAG_INT,   &cc.plan.omicron_letters, NULL,      0 },
        { "--target",   FLAG_CALL,  NULL,                     h_target,  0 },
        { "--dry-run",  FLAG_BOOL,  &cc.plan.dry_run,         NULL,      0 },
        { "--av",       FLAG_BOOL,  &cc.av,                   NULL,      0 },
    };
    CoreFlags core;
    core_flags_defaults(&core);
    char **pos = NULL;
    int npos = 0;
    int r = flags_parse(argc, argv, 2, tbl, 7, &cc, &core, &pos, &npos);
    if (r == -2) { usage(argv[0]); free_pos(pos); return 0; }
    if (r < 0) { free_pos(pos); return 1; }

    if (npos >= 1) {
        int s = plan_style_by_name(pos[0]);
        if (s < 0) die("unknown style '%s' (day|storm|drift|pulse|rupture|strata)", pos[0]);
        cc.plan.style = s;
    }
    if (npos == 2) {
        char *end;
        cc.plan.seed = strtoull(pos[1], &end, 10);
        if (end == pos[1] || *end != '\0') die("invalid seed '%s'", pos[1]);
        cc.plan.have_seed = 1;
    } else if (npos > 2) {
        die("unexpected argument '%s' (usage: STYLE SEED)", pos[2]);
    }
    free_pos(pos);

    if (core.out) snprintf(cc.plan.out_prefix, sizeof(cc.plan.out_prefix), "%s", core.out);
    if (core.max > 0) cc.plan.max_files = (int)core.max;
    if (core.have_seed) { cc.plan.seed = core.seed; cc.plan.have_seed = 1; }

    plan_prepare(&cc.plan, &cc);

    if (do_compose)
        return compose_run(&cc);

    /* plan-only */
    TrackList mus = { 0 }, fld = { 0 };
    library_load(&mus, cc.plan.mus_dir, "music", cc.plan.max_files);
    library_load(&fld, cc.plan.fld_dir, "field", cc.plan.max_files);
    printf("analyzing (~/.cache/tj):\n");
    library_analyze(&mus, "music");
    library_analyze(&fld, "field");

    PlanResult res;
    plan_run(&cc.plan, &mus, &fld, &res);
    for (int p = 0; p < cc.plan.parts; p++) {
        char path[760];
        snprintf(path, sizeof(path), "%s_part%02d_music.edl", cc.plan.out_prefix, p + 1);
        write_simple(path, res.music_edls[p]);
        snprintf(path, sizeof(path), "%s_part%02d_field.edl", cc.plan.out_prefix, p + 1);
        write_simple(path, res.field_edls[p]);
        snprintf(path, sizeof(path), "%s_part%02d.arc", cc.plan.out_prefix, p + 1);
        write_simple(path, res.arcs[p]);
        if (res.vedls[p] && res.vedls[p][0]) {
            snprintf(path, sizeof(path), "%s_part%02d_music.vedl", cc.plan.out_prefix, p + 1);
            write_simple(path, res.vedls[p]);
        }
    }
    printf("plans written: %s_partNN_{music,field}.edl\n", cc.plan.out_prefix);
    plan_free(&cc.plan, &res);
    free(mus.v);
    free(fld.v);
    return 0;
}

static int cmd_av(int argc, char **argv)
{
    const char *vedl = NULL, *arc = NULL, *vid = NULL;
    AvOpts o;
    av_opts_defaults(&o);
    FlagSpec tbl[] = {
        { "--vedl", FLAG_STR, &vedl, NULL, 0 },
        { "--arc",  FLAG_STR, &arc,  NULL, 0 },
        { "--vid",  FLAG_STR, &vid,  NULL, 0 },
    };
    CoreFlags core;
    core_flags_defaults(&core);
    char **pos = NULL;
    int npos = 0;
    int r = flags_parse(argc, argv, 2, tbl, 3, NULL, &core, &pos, &npos);
    if (r == -2) { usage(argv[0]); free_pos(pos); return 0; }
    if (r < 0) { free_pos(pos); return 1; }

    avopts_from_core(&o, &core);
    const char *edl = npos > 0 ? pos[0] : NULL;
    const char *out = core.out ? core.out : (npos > 1 ? pos[1] : NULL);
    free_pos(pos);

    if (!edl || !out) {
        fprintf(stderr, "usage: gram av \"<edl>\" out.mp4 [--vedl F] [--arc S] "
                        "[--vid DIR] [--w W] [--h H] [--fps N]\n");
        return 1;
    }

    /* video pool resolution: --vid > $GRAM_VID > conf vid= > default */
    if (!vid) vid = resolve_vid();

    /* audio sidecar convention: <out>_audio.wav must exist next to output */
    char audio_wav[1024];
    snprintf(audio_wav, sizeof(audio_wav), "%s", out);
    size_t l = strlen(audio_wav);
    if (l > 4 && strcmp(audio_wav + l - 4, ".mp4") == 0)
        snprintf(audio_wav + l - 4, sizeof(audio_wav) - (l - 4), "_audio.wav");
    else
        strncat(audio_wav, "_audio.wav", sizeof(audio_wav) - strlen(audio_wav) - 1);
    if (access(audio_wav, F_OK) != 0)
        die("audio mix '%s' not found — render it first with 'gram render', or use compose --av",
            audio_wav);
    return av_render(edl, vedl, arc, audio_wav, vid, out, &o);
}

static int cmd_edit(int argc, char **argv)
{
    const char *vid = NULL, *edl_dump = NULL;
    int max_files = 1000;
    AvOpts o;
    av_opts_defaults(&o);
    EditCfg ec;
    edit_cfg_defaults(&ec);

    FlagSpec tbl[] = {
        { "--vid",  FLAG_STR,   &vid,      NULL, 0 },
        { "--edl",  FLAG_STR,   &edl_dump, NULL, 0 },
        { "--span", FLAG_FLOAT, &ec.span,  NULL, 0 },
    };
    CoreFlags core;
    core_flags_defaults(&core);
    char **pos = NULL;
    int npos = 0;
    int r = flags_parse(argc, argv, 2, tbl, 3, NULL, &core, &pos, &npos);
    if (r == -2) { usage(argv[0]); free_pos(pos); return 0; }
    if (r < 0) { free_pos(pos); return 1; }

    avopts_from_core(&o, &core);
    if (core.max > 0) max_files = (int)core.max;
    const char *out = core.out ? core.out : (npos > 0 ? pos[0] : NULL);
    free_pos(pos);

    if (!out) {
        fprintf(stderr, "usage: gram edit out.mp4 [--vid DIR] [--w W] [--h H] "
                        "[--fps N] [--span S] [--max N] [--edl FILE] [--mute] < text\n");
        return 1;
    }

    /* resolution order: --vid > $GRAM_VID > conf vid= > default pool */
    if (!vid) vid = resolve_vid();
    return edit_run(vid, &ec, out, o.w, o.h, o.fps, max_files, edl_dump, core.mute);
}

static int cmd_slides(int argc, char **argv)
{
    const char *img = NULL, *fld = NULL;
    int max_images = 0;
    AvOpts o;
    av_opts_defaults(&o);
    double dur = SLIDES_DEFAULT_DUR;
    SlidesOpts so = { 0 };

    FlagSpec tbl[] = {
        { "--img",         FLAG_STR,   &img,          NULL, 0 },
        { "--fld",         FLAG_STR,   &fld,          NULL, 0 },
        { "--dur",         FLAG_FLOAT, &dur,          NULL, 0 },
        { "--dada",        FLAG_BOOL,  &so.dada,      NULL, 0 },
        { "--title",       FLAG_STR,   &so.title,     NULL, 0 },
        { "--title-slots", FLAG_INT,   &so.title_slots, NULL, 0 },
        { "--title-px",    FLAG_FLOAT, &so.title_px,  NULL, 0 },
        { "--stone",       FLAG_STR,   &so.stone,     NULL, 0 },
        { "--dada-bin",    FLAG_STR,   &so.dada_bin,  NULL, 0 },
    };
    CoreFlags core;
    core_flags_defaults(&core);
    char **pos = NULL;
    int npos = 0;
    int r = flags_parse(argc, argv, 2, tbl, 9, NULL, &core, &pos, &npos);
    if (r == -2) { usage(argv[0]); free_pos(pos); return 0; }
    if (r < 0) { free_pos(pos); return 1; }

    avopts_from_core(&o, &core);
    if (core.max > 0) max_images = (int)core.max;
    uint64_t seed = core.seed;
    int have_seed = core.have_seed;
    const char *out = core.out ? core.out : (npos > 0 ? pos[0] : NULL);
    free_pos(pos);

    if (!out || !img) {
        fprintf(stderr, "usage: gram slides out.mp4 --img DIR [--fld DIR] [--w W] [--h H] "
                        "[--fps N] [--dur S] [--seed N] [--max N] [--mute]\n"
                        "  --dada           deterministic stone-oracle film (60s)\n"
                        "  --title T        opening title text (drawn over the stone)\n"
                        "  --title-slots N  title card length in slides (default 6)\n"
                        "  --title-px P     title cap height (default 12)\n"
                        "  --stone FILE     stone image (default dada/noise.png)\n"
                        "  --dada-bin PATH  dada binary (default exe/dada/dada)\n");
        return 1;
    }

    if (!have_seed) {
        seed = (uint64_t)time(NULL) ^ ((uint64_t)getpid() << 32);
        printf("slides: seed %llu\n", (unsigned long long)seed);
    }

    if (!fld) fld = resolve_dir(NULL, "GRAM_FLD", "fld");
    return slides_run(img, fld, out, o.w, o.h, o.fps, dur, seed, max_images,
                      core.mute, &so);
}

/* `gram dada` — the committed film: exact 60s, 932x576@25, 0.216s slides,
 * kof26 opening title over the noise stone, pictures from the B&W film
 * scans in the central config's img= dir, dense field mix from fld=.
 * Reproduces byte-for-byte from the same inputs. */
static int cmd_dada(int argc, char **argv)
{
    const char *img = NULL, *fld = NULL;
    AvOpts o;
    av_opts_defaults(&o);

    SlidesOpts so = {
        .dada = 1,
        .title_slots = 6,
        .title = "kof26",
        .title_px = 12,
    };

    FlagSpec tbl[] = {
        { "--img",         FLAG_STR,   &img,          NULL, 0 },
        { "--fld",         FLAG_STR,   &fld,          NULL, 0 },
        { "--title",       FLAG_STR,   &so.title,     NULL, 0 },
        { "--title-slots", FLAG_INT,   &so.title_slots, NULL, 0 },
        { "--title-px",    FLAG_FLOAT, &so.title_px,  NULL, 0 },
        { "--stone",       FLAG_STR,   &so.stone,     NULL, 0 },
        { "--dada-bin",    FLAG_STR,   &so.dada_bin,  NULL, 0 },
    };
    CoreFlags core;
    core_flags_defaults(&core);
    char **pos = NULL;
    int npos = 0;
    int r = flags_parse(argc, argv, 2, tbl, 7, NULL, &core, &pos, &npos);
    if (r == -2) { usage(argv[0]); free_pos(pos); return 0; }
    if (r < 0) { free_pos(pos); return 1; }

    avopts_from_core(&o, &core);
    int max_images = core.max > 0 ? (int)core.max : 0;
    const char *out = core.out ? core.out : (npos > 0 ? pos[0] : NULL);
    free_pos(pos);

    if (!out) {
        fprintf(stderr, "usage: gram dada out.mp4 [--img DIR] [--fld DIR] [--title T] "
                        "[--title-slots N] [--title-px P] [--stone FILE] "
                        "[--dada-bin PATH] [--mute]\n");
        return 1;
    }

    if (!img) img = resolve_dir(NULL, "GRAM_IMG", "img");
    if (!fld) fld = resolve_dir(NULL, "GRAM_FLD", "fld");

    return slides_run(img, fld, out, o.w, o.h, o.fps, 0.216, core.seed,
                      max_images, core.mute, &so);
}

typedef struct {
    const char *lines[64];
    int n;
} TitleArgs;

static int h_title_line(void *ctx, const char *name, const char *val)
{
    TitleArgs *a = ctx;
    (void)name;
    if (!val) return -1;
    if (a->n >= 64) return -1;
    a->lines[a->n++] = val;
    return 0;
}

static int cmd_title(int argc, char **argv)
{
    TitleArgs ta = { { 0 }, 0 };
    const char *stone = NULL;
    double dur = 0.0;
    int size_px = 0;
    AvOpts o;
    av_opts_defaults(&o);

    FlagSpec tbl[] = {
        { "-t",      FLAG_CALL,  NULL,     h_title_line, 0 },
        { "--stone", FLAG_STR,   &stone,   NULL,         0 },
        { "-d",      FLAG_FLOAT, &dur,     NULL,         0 },
        { "-s",      FLAG_INT,   &size_px, NULL,         0 },
    };
    CoreFlags core;
    core_flags_defaults(&core);
    char **pos = NULL;
    int npos = 0;
    int r = flags_parse(argc, argv, 2, tbl, 4, &ta, &core, &pos, &npos);
    if (r == -2) { usage(argv[0]); free_pos(pos); return 0; }
    if (r < 0) { free_pos(pos); return 1; }

    avopts_from_core(&o, &core);
    const char *out = core.out ? core.out : (npos > 0 ? pos[0] : NULL);
    free_pos(pos);

    if (!out) {
        fprintf(stderr, "usage: gram title out.mp4 -t 'LINE' [-t ...] [-s PX] "
                        "[-d SECS] [--stone FILE] [--w W] [--h H] [--fps N] [--mute]\n");
        return 1;
    }
    if (ta.n == 0) {
        fprintf(stderr, "gram title: at least one -t 'LINE' required\n");
        return 1;
    }
    return title_run(ta.lines, ta.n, stone, out, o.w, o.h, o.fps, dur, size_px,
                     core.mute);
}

typedef struct {
    const char *srcs[64];
    int n;
} PartSrcArgs;

static int h_part_src(void *ctx, const char *name, const char *val)
{
    PartSrcArgs *a = ctx;
    (void)name;
    if (!val) return -1;
    if (a->n >= 64) return -1;
    a->srcs[a->n++] = val;
    return 0;
}

static int cmd_partition(int argc, char **argv)
{
    const char *pattern = NULL, *script = NULL, *edl_path = NULL, *mlt_path = NULL;
    int unit = 0, do_std = 0;
    PartSrcArgs pa = { { 0 }, 0 };
    AvOpts o;
    av_opts_defaults(&o);

    FlagSpec tbl[] = {
        { "-p",       FLAG_STR,  &pattern,   NULL, 0 },
        { "--script", FLAG_STR,  &script,    NULL, 0 },
        { "-i",       FLAG_CALL, NULL,       h_part_src, 0 },
        { "--unit",   FLAG_INT,  &unit,      NULL, 0 },
        { "--edl",    FLAG_STR,  &edl_path,  NULL, 0 },
        { "--mlt",    FLAG_STR,  &mlt_path,  NULL, 0 },
        { "--std",    FLAG_BOOL, &do_std,    NULL, 0 },
    };
    CoreFlags core;
    core_flags_defaults(&core);
    char **pos = NULL;
    int npos = 0;
    int r = flags_parse(argc, argv, 2, tbl, 7, &pa, &core, &pos, &npos);
    if (r == -2) { usage(argv[0]); free_pos(pos); return 0; }
    if (r < 0) { free_pos(pos); return 1; }

    avopts_from_core(&o, &core);
    const char *out = core.out ? core.out : (npos > 0 ? pos[0] : NULL);
    free_pos(pos);

    if (!pattern && !script) {
        fprintf(stderr, "usage: gram partition out.mp4 -p PATTERN | --script F "
                        "[-i FILE ...] [--unit N] [--std] [--edl F] [--mlt F]\n");
        return 1;
    }
    if (pa.n == 0 && !script) {
        fprintf(stderr, "gram partition: pattern mode needs at least one -i FILE\n");
        return 1;
    }
    if (!out && !edl_path && !mlt_path) {
        fprintf(stderr, "gram partition: give an output (out.mp4) or --edl F / --mlt F\n");
        return 1;
    }

    PCut *cuts = NULL;
    int ncuts = 0;
    double total = 0.0;
    if (part_plan(pattern, script, pa.srcs, pa.n, unit, o.fps,
                  &cuts, &ncuts, &total))
        return 1;

    int rc = 0;
    if (edl_path) rc |= part_write_edl(cuts, ncuts, pa.srcs, pa.n, edl_path, o.fps);
    if (mlt_path) rc |= part_write_mlt(cuts, ncuts, pa.srcs, pa.n, mlt_path,
                                       o.w, o.h, o.fps, do_std);
    if (out)      rc |= part_render(cuts, ncuts, pa.srcs, pa.n, out, o.w, o.h, o.fps);
    free(cuts);
    return rc != 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        return argc < 2 ? 1 : 0;
    }
    if (strcmp(argv[1], "omicron") == 0) return cmd_omicron(argc, argv);
    if (strcmp(argv[1], "analyze") == 0) return cmd_analyze(argc, argv);
    if (strcmp(argv[1], "render") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: gram render \"<edl>\" [out.wav] [opts]\n"); return 1; }
        const char *edl = argv[2];
        const char *out = NULL;
        int opt_start = 3;
        if (argc > 3 && argv[3][0] != '-') { out = argv[3]; opt_start = 4; }
        RenderOpts o;
        render_opts_defaults(&o);
        if (render_opts_parse(argc, argv, opt_start, &o)) return 1;
        return render_edl(edl, out, &o);
    }
    if (strcmp(argv[1], "plan") == 0) return cmd_plan_or_compose(argc, argv, 0);
    if (strcmp(argv[1], "compose") == 0) return cmd_plan_or_compose(argc, argv, 1);
    if (strcmp(argv[1], "av") == 0) return cmd_av(argc, argv);
    if (strcmp(argv[1], "edit") == 0) return cmd_edit(argc, argv);
    if (strcmp(argv[1], "slides") == 0) return cmd_slides(argc, argv);
    if (strcmp(argv[1], "dada") == 0) return cmd_dada(argc, argv);
    if (strcmp(argv[1], "title") == 0) return cmd_title(argc, argv);
    if (strcmp(argv[1], "partition") == 0) return cmd_partition(argc, argv);
    usage(argv[0]);
    return 1;
}