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
        "commands:\n"
        "  omicron [-n N] [--reverse R] [--limit K] [--force]\n"
        "      enumerate operator expressions over letters a..z = 1..26\n"
        "  analyze <file>...\n"
        "      BPM / Camelot key / rhythmic texture (cached in ~/.cache/tj)\n"
        "  render \"<edl>\" [out.wav] [--bpm auto|N] [--snap] [--keylock auto|K]\n"
        "          [--fade-in S] [--fade-out S] [--arc t:g,...] [--master pop|subtle]\n"
        "      mix an EDL to a 48k stereo WAV (supports string, file.edl, or stdin -)\n"
        "  plan <style> [seed] [--parts N] [--len S] [--out PREFIX] [--dry-run]\n"
        "       [--engine rng|omicron] [--letters N] [--target R] [--max N] [--av]\n"
        "      plan movements, write .edl (+.vedl) sidecars only\n"
        "  av \"<edl>\" out.mp4 [--vedl F] [--arc t:g,...] [--vid DIR]\n"
        "     [--w W] [--h H] [--fps N]\n"
        "      render the visual edit of an EDL, muxed with <out>_audio.wav\n"
        "  edit out.mp4 [-t 'TEXT' | -f FILE] [--vid DIR] [--w W] [--h H] [--fps N]\n"
        "       [--span S] [--max N] [--edl FILE] [--mute]\n"
        "      text-driven video edit: letters a..z pick clips from pool;\n"
        "      reads text from stdin, -t 'TEXT', or -f FILE\n"
        "  slides out.mp4 --img DIR [--fld DIR] [--w W] [--h H] [--fps N]\n"
        "       [--dur S] [--seed N] [--max N] [--mute]\n"
        "       [--dada] [--title T] [--title-slots N] [--title-px P]\n"
        "       [--stone FILE] [--dada-bin PATH]\n"
        "      picture slideshow: crop/fill to format, grayscale, shuffled,\n"
        "      muxed with field recordings; 0.432s per slide by default\n"
        "  dada out.mp4 [--img DIR] [--fld DIR] [--title T] [--title-slots N]\n"
        "       [--title-px P] [--stone FILE] [--dada-bin PATH] [--mute]\n"
        "      the committed dada film (60s, 932x576@25, 0.216s slides, kof26\n"
        "      title over the noise stone, B&W Camera pictures, dense field mix)\n"
        "  compose <style> [seed] [--parts N] [--len S] [--out PREFIX] [--dry-run]\n"
        "          [--engine rng|omicron] [--letters N] [--target R] [--max N] [--av]\n"
        "      full pipeline: libraries -> plan -> render -> master -> mp4\n"
        "  title out.mp4 -t 'LINE' [-t ...] [-f FILE] [-s PX] [-d SECS] [--stone FILE]\n"
        "       [--w W] [--h H] [--fps N] [--mute]\n"
        "      Gomotor title card: white text on black (or a greyscale --stone)\n"
        "  partition out.mp4 -p PATTERN | --script F [-i FILE ...] [--unit N]\n"
        "       [--std] [--edl F] [--mlt F] [--w W] [--h H] [--fps N] [--mute]\n"
        "      transport-DSL crosscut (open/jf/jb/pf/x/H) rendered silently;\n"
        "      supports script from file or stdin (--script -)\n"
        "\n"
        "styles: day | storm | drift | pulse | rupture | strata\n"
        "config: central ~/.config/gram.conf (mus= fld= vid= img=; michacka reads\n"
        "mus= fld= tj= from it too); path override env GRAM_CONF; per-key env\n"
        "GRAM_MUS / GRAM_FLD / GRAM_VID / GRAM_IMG; missing keys are errors.\n"
        "help: 'gram help <command>' or 'gram <command> --help' for details.\n",
        prog);
}

static void help_cmd(const char *cmd)
{
    if (!cmd || !cmd[0]) {
        usage("gram");
        return;
    }
    if (strcmp(cmd, "omicron") == 0) {
        fprintf(stderr,
            "usage: gram omicron [-n N] [--reverse R] [--limit K] [--force]\n\n"
            "Enumerate operator expressions over letters a..z = 1..26 with + − × ÷.\n\n"
            "Options:\n"
            "  -n N           letter count (1..26, default 26)\n"
            "  --reverse R    search for expressions evaluating to target number R\n"
            "  --limit K      maximum expressions to output (default 1000)\n"
            "  --max K        alias for --limit\n"
            "  --force        force enumeration even if combination count > 1M\n");
    } else if (strcmp(cmd, "analyze") == 0) {
        fprintf(stderr,
            "usage: gram analyze <file>...\n\n"
            "Analyze audio files for BPM, Camelot harmonic key, and texture metrics.\n"
            "Results are cached in $XDG_CACHE_HOME/tj or ~/.cache/tj.\n");
    } else if (strcmp(cmd, "render") == 0) {
        fprintf(stderr,
            "usage: gram render \"<edl>\" [out.wav] [opts]\n"
            "       gram render file.edl [out.wav] [opts]\n"
            "       cat file.edl | gram render - [out.wav] [opts]\n\n"
            "Offline EDL mixdown engine (tj-compatible) rendered into 48k stereo WAV.\n\n"
            "Options:\n"
            "  --out PATH            output WAV file path (alternative to positional out.wav)\n"
            "  --bpm auto|N          tempo matching (auto = median BPM, or fixed BPM)\n"
            "  --snap                snap in/out/at points to the tempo beat grid\n"
            "  --keylock auto|KEY    Camelot key harmonization (auto or specific key e.g. 8A)\n"
            "  --fade-in S           default fade-in duration in seconds (default 0.1)\n"
            "  --fade-out S          default fade-out duration in seconds (default 0.1)\n"
            "  --arc t:g,...         gain automation arc curve (e.g. 0:0.5,10:1.0,20:0.2)\n"
            "  --master pop|subtle   apply mastering chain with brickwall limiting\n");
    } else if (strcmp(cmd, "plan") == 0) {
        fprintf(stderr,
            "usage: gram plan <style> [seed] [--parts N] [--len S] [--out PREFIX] [--dry-run]\n"
            "                 [--engine rng|omicron] [--letters N] [--target R] [--max N] [--av]\n\n"
            "Plan multi-movement stochastic/combinatorial compositions (.edl + .arc sidecars).\n\n"
            "Styles: day | storm | drift | pulse | rupture | strata\n"
            "Engines: rng (bit-exact michacka PRNG) | omicron (deterministic enumeration)\n");
    } else if (strcmp(cmd, "compose") == 0) {
        fprintf(stderr,
            "usage: gram compose <style> [seed] [...same options as plan]\n\n"
            "Full pipeline: library scan -> plan -> audio render -> master -> video render (with --av).\n");
    } else if (strcmp(cmd, "av") == 0) {
        fprintf(stderr,
            "usage: gram av \"<edl>\" out.mp4 [--vedl F] [--arc S] [--vid DIR]\n"
            "               [--w W] [--h H] [--fps N] [--mute]\n\n"
            "Render visual edit corresponding to an EDL, muxed with audio.\n"
            "Supports file path or stdin (-) for EDL and VEDL.\n");
    } else if (strcmp(cmd, "edit") == 0) {
        fprintf(stderr,
            "usage: gram edit out.mp4 [-t 'TEXT' | -f FILE] [--vid DIR] [--w W] [--h H]\n"
            "                 [--fps N] [--span S] [--max N] [--edl FILE] [--mute]\n\n"
            "Text-driven deterministic video edit. Reads text from stdin, -t 'TEXT', or -f FILE.\n"
            "Letters a..z pick clips from pool; position sets in-point via golden ratio scatter.\n");
    } else if (strcmp(cmd, "slides") == 0) {
        fprintf(stderr,
            "usage: gram slides out.mp4 --img DIR [--fld DIR] [--w W] [--h H] [--fps N]\n"
            "                   [--dur S] [--seed N] [--max N] [--mute]\n"
            "                   [--dada] [--title T] [--title-slots N] [--title-px P]\n"
            "                   [--stone FILE] [--dada-bin PATH]\n\n"
            "Grayscale photo slideshow from images in DIR, crop/filled and muxed with field recordings.\n");
    } else if (strcmp(cmd, "dada") == 0) {
        fprintf(stderr,
            "usage: gram dada out.mp4 [--img DIR] [--fld DIR] [--title T] [--title-slots N]\n"
            "                 [--title-px P] [--stone FILE] [--dada-bin PATH] [--mute]\n\n"
            "Committed, deterministic 60s stone-oracle film (932x576@25, noise stone oracle).\n");
    } else if (strcmp(cmd, "title") == 0) {
        fprintf(stderr,
            "usage: gram title out.mp4 -t 'LINE' [-t ...] [-f FILE] [-s PX] [-d SECS]\n"
            "                  [--stone FILE] [--w W] [--h H] [--fps N] [--mute]\n\n"
            "Gomotor stroke font intertitle cards over black or stone texture.\n"
            "Accepts lines from -t, -f FILE, or standard input.\n");
    } else if (strcmp(cmd, "partition") == 0) {
        fprintf(stderr,
            "usage: gram partition out.mp4 -p PATTERN | --script F [-i FILE ...] [--unit N]\n"
            "                      [--std] [--edl F] [--mlt F] [--w W] [--h H] [--fps N] [--mute]\n\n"
            "Transport DSL crosscut language (open/jf/jb/pf/x/H) across video sources.\n"
            "Supports script from file or stdin (--script -).\n");
    } else {
        usage("gram");
    }
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
        { "--limit",    FLAG_LONG, &a.limit,  NULL,      0 },
        { "--force",    FLAG_BOOL, &a.force,  NULL,      0 },
    };
    CoreFlags core;
    core_flags_defaults(&core);
    char **pos = NULL;
    int npos = 0;
    int r = flags_parse(argc, argv, 2, tbl, 4, &a, &core, &pos, &npos);
    free_pos(pos);
    if (r == -2) { help_cmd("omicron"); return 0; }
    if (r < 0) return 1;
    if (npos > 0) {
        help_cmd("omicron");
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
        long long rem = a.limit > count ? a.limit - count : 0;
        long long want = rem < 4096 ? rem : 4096;
        if (want <= 0) break;
        long long got = omicron_collect(a.n, a.has_target, a.target, want, want, buf);
        for (long long i = 0; i < got && count < a.limit; i++) {
            char line[512];
            omicron_format(&buf[i], line, sizeof(line));
            printf("%s\n", line);
            count++;
        }
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
    if (r == -2) { help_cmd("analyze"); free_pos(pos); return 0; }
    if (r < 0) { free_pos(pos); return 1; }
    if (npos < 1) {
        help_cmd("analyze");
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

static int cmd_render(int argc, char **argv)
{
    RenderOpts o;
    render_opts_defaults(&o);
    CoreFlags core;
    core_flags_defaults(&core);
    char **pos = NULL;
    int npos = 0;

    int r = flags_parse(argc, argv, 2,
                        RENDER_FLAGS, N_RENDER_FLAGS,
                        &o, &core, &pos, &npos);
    if (r == -2) { help_cmd("render"); free_pos(pos); return 0; }
    if (r < 0) { free_pos(pos); return 1; }

    const char *edl_arg = npos > 0 ? pos[0] : NULL;
    const char *out_file = core.out ? core.out : (npos > 1 ? pos[1] : NULL);

    char *edl_content = NULL;
    if (edl_arg && strcmp(edl_arg, "-") != 0) {
        edl_content = read_file_or_string(edl_arg);
    } else {
        if (edl_arg || !isatty(STDIN_FILENO)) {
            edl_content = read_stream_all(stdin);
        }
    }

    if (!edl_content || !edl_content[0]) {
        help_cmd("render");
        free(edl_content);
        free_pos(pos);
        return 1;
    }

    int rc = render_edl(edl_content, out_file, &o);
    free(edl_content);
    free_pos(pos);
    return rc;
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
    if (r == -2) { help_cmd(do_compose ? "compose" : "plan"); free_pos(pos); return 0; }
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
    if (r == -2) { help_cmd("av"); free_pos(pos); return 0; }
    if (r < 0) { free_pos(pos); return 1; }

    avopts_from_core(&o, &core);
    const char *edl = npos > 0 ? pos[0] : NULL;
    const char *out = core.out ? core.out : (npos > 1 ? pos[1] : NULL);
    free_pos(pos);

    if (!edl && !isatty(STDIN_FILENO)) edl = "-";

    if (!edl || !out) {
        help_cmd("av");
        return 1;
    }

    char *edl_content = read_file_or_string(edl);
    if (!edl_content || !edl_content[0]) {
        fprintf(stderr, "gram av: empty EDL input\n");
        free(edl_content);
        return 1;
    }

    char *vedl_content = vedl ? read_file_or_string(vedl) : NULL;

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
    int rc = av_render(edl_content, vedl_content, arc, audio_wav, vid, out, &o);
    free(edl_content);
    free(vedl_content);
    return rc;
}

static int cmd_edit(int argc, char **argv)
{
    const char *vid = NULL, *edl_dump = NULL;
    const char *text_arg = NULL, *file_arg = NULL;
    int max_files = 1000;
    AvOpts o;
    av_opts_defaults(&o);
    EditCfg ec;
    edit_cfg_defaults(&ec);

    FlagSpec tbl[] = {
        { "--vid",  FLAG_STR,   &vid,      NULL, 0 },
        { "--edl",  FLAG_STR,   &edl_dump, NULL, 0 },
        { "--span", FLAG_FLOAT, &ec.span,  NULL, 0 },
        { "-t",     FLAG_STR,   &text_arg, NULL, 0 },
        { "--text", FLAG_STR,   &text_arg, NULL, 0 },
        { "-f",     FLAG_STR,   &file_arg, NULL, 0 },
    };
    CoreFlags core;
    core_flags_defaults(&core);
    char **pos = NULL;
    int npos = 0;
    int r = flags_parse(argc, argv, 2, tbl, 6, NULL, &core, &pos, &npos);
    if (r == -2) { help_cmd("edit"); free_pos(pos); return 0; }
    if (r < 0) { free_pos(pos); return 1; }

    avopts_from_core(&o, &core);
    if (core.max > 0) max_files = (int)core.max;
    const char *out = core.out ? core.out : (npos > 0 ? pos[0] : NULL);
    if (!text_arg && npos > 1) text_arg = pos[1];
    free_pos(pos);

    if (!out) {
        help_cmd("edit");
        return 1;
    }

    char *text_content = NULL;
    if (file_arg) {
        text_content = read_file_or_string(file_arg);
    } else if (text_arg) {
        text_content = xstrdup(text_arg);
    }

    /* resolution order: --vid > $GRAM_VID > conf vid= > default pool */
    if (!vid) vid = resolve_vid();
    int rc = edit_run_text(vid, &ec, out, o.w, o.h, o.fps, max_files, edl_dump, core.mute, text_content);
    free(text_content);
    return rc;
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
    if (r == -2) { help_cmd("slides"); free_pos(pos); return 0; }
    if (r < 0) { free_pos(pos); return 1; }

    avopts_from_core(&o, &core);
    if (core.max > 0) max_images = (int)core.max;
    uint64_t seed = core.seed;
    int have_seed = core.have_seed;
    const char *out = core.out ? core.out : (npos > 0 ? pos[0] : NULL);
    free_pos(pos);

    if (!out || !img) {
        help_cmd("slides");
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
    if (r == -2) { help_cmd("dada"); free_pos(pos); return 0; }
    if (r < 0) { free_pos(pos); return 1; }

    avopts_from_core(&o, &core);
    int max_images = core.max > 0 ? (int)core.max : 0;
    const char *out = core.out ? core.out : (npos > 0 ? pos[0] : NULL);
    free_pos(pos);

    if (!out) {
        help_cmd("dada");
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
    const char *stone = NULL, *file_arg = NULL;
    double dur = 0.0;
    int size_px = 0;
    AvOpts o;
    av_opts_defaults(&o);

    FlagSpec tbl[] = {
        { "-t",      FLAG_CALL,  NULL,      h_title_line, 0 },
        { "--stone", FLAG_STR,   &stone,    NULL,         0 },
        { "-d",      FLAG_FLOAT, &dur,      NULL,         0 },
        { "-s",      FLAG_INT,   &size_px,  NULL,         0 },
        { "-f",      FLAG_STR,   &file_arg, NULL,         0 },
    };
    CoreFlags core;
    core_flags_defaults(&core);
    char **pos = NULL;
    int npos = 0;
    int r = flags_parse(argc, argv, 2, tbl, 5, &ta, &core, &pos, &npos);
    if (r == -2) { help_cmd("title"); free_pos(pos); return 0; }
    if (r < 0) { free_pos(pos); return 1; }

    avopts_from_core(&o, &core);
    const char *out = core.out ? core.out : (npos > 0 ? pos[0] : NULL);
    free_pos(pos);

    if (!out) {
        help_cmd("title");
        return 1;
    }

    char *allocated_lines[64] = { 0 };
    int n_allocated = 0;

    if (ta.n == 0) {
        char *src_text = NULL;
        if (file_arg) {
            src_text = read_file_or_string(file_arg);
        } else if (!isatty(STDIN_FILENO)) {
            src_text = read_stream_all(stdin);
        }
        if (src_text) {
            char *save = NULL;
            for (char *line = strtok_r(src_text, "\r\n", &save); line && ta.n < 64; line = strtok_r(NULL, "\r\n", &save)) {
                trim_ws(line);
                if (line[0]) {
                    allocated_lines[n_allocated] = xstrdup(line);
                    ta.lines[ta.n++] = allocated_lines[n_allocated++];
                }
            }
            free(src_text);
        }
    }

    if (ta.n == 0) {
        fprintf(stderr, "gram title: at least one line required (via -t, -f, or stdin)\n");
        return 1;
    }
    int rc = title_run(ta.lines, ta.n, stone, out, o.w, o.h, o.fps, dur, size_px,
                      core.mute);
    for (int i = 0; i < n_allocated; i++) free(allocated_lines[i]);
    return rc;
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
    if (r == -2) { help_cmd("partition"); free_pos(pos); return 0; }
    if (r < 0) { free_pos(pos); return 1; }

    avopts_from_core(&o, &core);
    const char *out = core.out ? core.out : (npos > 0 ? pos[0] : NULL);
    free_pos(pos);

    if (!pattern && !script && !isatty(STDIN_FILENO)) {
        script = "-";
    }

    if (!pattern && !script) {
        help_cmd("partition");
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
    if (strcmp(argv[1], "help") == 0) {
        help_cmd(argc > 2 ? argv[2] : NULL);
        return 0;
    }
    if (strcmp(argv[1], "omicron") == 0) return cmd_omicron(argc, argv);
    if (strcmp(argv[1], "analyze") == 0) return cmd_analyze(argc, argv);
    if (strcmp(argv[1], "render") == 0) return cmd_render(argc, argv);
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