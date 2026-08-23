#include "analysis.h"
#include "av_render.h"
#include "compose.h"
#include "library.h"
#include "omicron.h"
#include "plan.h"
#include "render.h"
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
        "      render the visual edit of an EDL, muxed with <out>_audio.wav\n"
        "  compose <style> [seed] [--parts N] [--len S] [--out PREFIX] [--dry-run]\n"
        "          [--engine rng|omicron] [--letters N] [--target R] [--max N] [--av]\n"
        "      full pipeline: libraries -> plan -> render -> master -> mp4\n"
        "\n"
        "styles: day | storm | drift | pulse | rupture\n"
        "config: ~/.config/gram.conf (mus= fld= vid=), falls back to michacka.conf;\n"
        "env GRAM_MUS / GRAM_FLD / GRAM_VID override.\n",
        prog);
}

/* ------------------------------------------------------------------ */
/* config                                                              */

typedef struct {
    char *mus, *fld, *vid;
} GramConf;

static void conf_load(GramConf *c)
{
    char path[1024];
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0])
        snprintf(path, sizeof(path), "%s/gram.conf", xdg);
    else
        snprintf(path, sizeof(path), "%s/.config/gram.conf", home ? home : ".");
    FILE *fp = fopen(path, "r");
    if (!fp) {
        if (xdg && xdg[0])
            snprintf(path, sizeof(path), "%s/michacka.conf", xdg);
        else
            snprintf(path, sizeof(path), "%s/.config/michacka.conf", home ? home : ".");
        fp = fopen(path, "r");
        if (!fp) return;
    }
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
        else if (strcmp(key, "tj") == 0) { /* legacy key, engine is internal now */ }
        else die("%s: unknown key '%s' (expected mus|fld|vid|tj)", path, key);
    }
    fclose(fp);
}

/* ------------------------------------------------------------------ */
/* subcommands                                                         */

static int cmd_omicron(int argc, char **argv)
{
    int n_letters = OMICRON_MAX_LETTERS;
    int has_target = 0;
    int force = 0;
    double target = 0.0;
    long long limit = 1000;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            n_letters = atoi(argv[++i]);
            if (n_letters < 1 || n_letters > OMICRON_MAX_LETTERS) return 1;
        } else if (strcmp(argv[i], "--reverse") == 0 && i + 1 < argc) {
            target = atof(argv[++i]);
            has_target = 1;
        } else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            limit = strtoll(argv[++i], NULL, 10);
            if (limit < 1) return 1;
        } else if (strcmp(argv[i], "--force") == 0) {
            force = 1;
        } else {
            return 1;
        }
    }

    unsigned long long combos = 1;
    for (int i = 2; i <= n_letters; i++) combos *= 4;
    if (!has_target && combos > 1048576ULL && !force) {
        printf("a..%c: %d gaps x 4 operations = %llu possible expressions\n",
               'a' + n_letters - 1, n_letters - 1, combos);
        printf("too many to print; try:\n");
        printf("  gram omicron --reverse <number>   find expressions equal to it\n");
        printf("  gram omicron -n <N>              enumerate a smaller prefix\n");
        printf("  gram omicron -n <N> --force      print everything anyway\n");
        return 0;
    }

    long long count = 0;
    OmicronExpr *buf = xmalloc(sizeof(OmicronExpr) * 4096);
    for (;;) {
        long long want = has_target ? (limit - count < 4096 ? limit - count : 4096) : 4096;
        if (want <= 0) break;
        long long got = omicron_collect(n_letters, has_target, target, want, want, buf);
        for (long long i = 0; i < got; i++) {
            char line[512];
            omicron_format(&buf[i], line, sizeof(line));
            printf("%s\n", line);
        }
        count += got;
        if (got < want || count >= limit) break;
        if (!has_target && count >= (long long)combos) break;
    }
    free(buf);
    if (has_target && count == 0)
        printf("no expression found for result %.7g\n", target);
    return 0;
}

static void cmd_analyze(int argc, char **argv)
{
    for (int i = 2; i < argc; i++) {
        float bpm = 0.0f;
        char key[32] = "?";
        Texture tex;
        memset(&tex, 0, sizeof(tex));
        tex.rhythm = -1.0f;
        analyze_track(argv[i], &bpm, key, sizeof(key), &tex);
        printf("%-64s %6.1f BPM  %-4s  d=%.2f/s pulse=%.2f steady=%.2f  %s\n",
               argv[i], bpm, key, tex.density, tex.pulse, tex.steady,
               texture_label(tex.rhythm));
    }
}

static void write_simple(const char *path, const char *content)
{
    FILE *fp = fopen(path, "w");
    if (!fp) die("cannot write %s", path);
    fprintf(fp, "%s\n", content);
    fclose(fp);
}

static void plan_arg(PlanCfg *cfg, ComposeCfg *cc, const char *flag,
                     const char *val, int *pos_args, const char *arg)
{
    (void)val;
    if (strcmp(flag, "--parts") == 0) cfg->parts = atoi(val);
    else if (strcmp(flag, "--len") == 0) cfg->part_len = atof(val);
    else if (strcmp(flag, "--out") == 0) snprintf(cfg->out_prefix, sizeof(cfg->out_prefix), "%s", val);
    else if (strcmp(flag, "--dry-run") == 0) cfg->dry_run = 1;
    else if (strcmp(flag, "--engine") == 0) {
        if (strcmp(val, "rng") == 0) cfg->engine = PLAN_ENGINE_RNG;
        else if (strcmp(val, "omicron") == 0) cfg->engine = PLAN_ENGINE_OMICRON;
        else die("unknown engine '%s' (rng|omicron)", val);
    }
    else if (strcmp(flag, "--letters") == 0) cfg->omicron_letters = atoi(val);
    else if (strcmp(flag, "--target") == 0) { cfg->has_target = 1; cfg->target = atof(val); }
    else if (strcmp(flag, "--max") == 0) cfg->max_files = atoi(val);
    else if (strcmp(flag, "--av") == 0) cc->av = 1;
    else {
        /* positional */
        if (*pos_args == 0) {
            int s = plan_style_by_name(arg);
            if (s < 0) die("unknown style '%s' (day|storm|drift|pulse|rupture)", arg);
            cfg->style = s;
        } else if (*pos_args == 1) {
            char *end;
            cfg->seed = strtoull(arg, &end, 10);
            if (end == arg || *end != '\0') die("invalid seed '%s'", arg);
            cfg->have_seed = 1;
        } else {
            die("unexpected argument '%s' (usage: STYLE SEED)", arg);
        }
        (*pos_args)++;
    }
}

static void plan_prepare(PlanCfg *cfg, ComposeCfg *cc, const GramConf *conf)
{
    if (!cfg->mus_dir) {
        if (conf->mus) cfg->mus_dir = conf->mus;
        else {
            static char buf[1024];
            snprintf(buf, sizeof(buf), "%s/recordings", getenv("HOME") ? getenv("HOME") : ".");
            cfg->mus_dir = buf;
        }
    }
    if (!cfg->fld_dir) {
        if (conf->fld) cfg->fld_dir = conf->fld;
        else {
            static char buf[1024];
            snprintf(buf, sizeof(buf), "%s", "/mnt/data/recordings/field");
            cfg->fld_dir = buf;
        }
    }
    if (!cc->vid_dir[0]) {
        if (conf->vid) snprintf(cc->vid_dir, sizeof(cc->vid_dir), "%s", conf->vid);
        else snprintf(cc->vid_dir, sizeof(cc->vid_dir), "%s", "/mnt/data/recordings/video8");
    }

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
    GramConf conf = { 0 };
    conf_load(&conf);

    ComposeCfg cc;
    memset(&cc, 0, sizeof(cc));
    plan_cfg_defaults(&cc.plan);
    cc.plan.style = ST_DAY;
    cc.plan.engine = PLAN_ENGINE_RNG;

    int pos = 0;
    for (int i = 2; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == '-') {
            const char *flag = argv[i];
            const char *val = NULL;
            if (strcmp(flag, "--parts") == 0 || strcmp(flag, "--len") == 0 ||
                strcmp(flag, "--out") == 0 || strcmp(flag, "--engine") == 0 ||
                strcmp(flag, "--letters") == 0 || strcmp(flag, "--target") == 0 ||
                strcmp(flag, "--max") == 0) {
                if (i + 1 >= argc) die("%s requires a value", flag);
                val = argv[++i];
            } else {
                val = "";
            }
            plan_arg(&cc.plan, &cc, flag, val, &pos, "");
        } else if (argv[i][0] != '-') {
            plan_arg(&cc.plan, &cc, "", "", &pos, argv[i]);
        } else {
            die("unknown option '%s' (try --help)", argv[i]);
        }
    }

    plan_prepare(&cc.plan, &cc, &conf);

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
    if (argc < 4) {
        fprintf(stderr, "usage: gram av \"<edl>\" out.mp4 [--vedl F] [--arc S] "
                        "[--vid DIR] [--w W] [--h H] [--fps N]\n");
        return 1;
    }
    const char *edl = argv[2];
    const char *out = argv[3];
    const char *vedl = NULL, *arc = NULL, *vid = NULL;
    AvOpts o;
    av_opts_defaults(&o);
    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "--vedl") == 0 && i + 1 < argc) vedl = argv[++i];
        else if (strcmp(argv[i], "--arc") == 0 && i + 1 < argc) arc = argv[++i];
        else if (strcmp(argv[i], "--vid") == 0 && i + 1 < argc) vid = argv[++i];
        else if (strcmp(argv[i], "--w") == 0 && i + 1 < argc) o.w = atoi(argv[++i]);
        else if (strcmp(argv[i], "--h") == 0 && i + 1 < argc) o.h = atoi(argv[++i]);
        else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) o.fps = atoi(argv[++i]);
        else { fprintf(stderr, "gram av: unknown option '%s'\n", argv[i]); return 1; }
    }
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

int main(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        return argc < 2 ? 1 : 0;
    }
    if (strcmp(argv[1], "omicron") == 0) return cmd_omicron(argc, argv);
    if (strcmp(argv[1], "analyze") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: gram analyze <file>...\n"); return 1; }
        cmd_analyze(argc, argv);
        return 0;
    }
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
    usage(argv[0]);
    return 1;
}
