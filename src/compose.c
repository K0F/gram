#include "compose.h"

#include "av_render.h"
#include "render.h"
#include "util.h"

#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void compose_resolve_dirs(ComposeCfg *cc)
{
    char buf[1024];
    if (!cc->plan.mus_dir) {
        const char *env = getenv("GRAM_MUS");
        const char *home = getenv("HOME");
        snprintf(buf, sizeof(buf), "%s", env ? env : (home ? home : "."));
        strncat(buf, "/recordings", sizeof(buf) - strlen(buf) - 1);
        cc->plan.mus_dir = xstrdup(buf);
    }
    if (!cc->plan.fld_dir) {
        const char *env = getenv("GRAM_FLD");
        cc->plan.fld_dir = xstrdup(env ? env : "/mnt/data/recordings/field");
    }
    if (!cc->vid_dir[0]) {
        const char *env = getenv("GRAM_VID");
        snprintf(cc->vid_dir, sizeof(cc->vid_dir), "%s",
                 env ? env : "/mnt/data/recordings/video8");
    }
}

static void write_file(const char *path, const char *content)
{
    FILE *fp = fopen(path, "w");
    if (!fp) die("cannot write %s: %s", path, strerror(errno));
    fprintf(fp, "%s\n", content);
    fclose(fp);
}

static int render_part_audio(const PlanCfg *cfg, const StyleSpec *st,
                             const char *music_edl, const char *field_edl,
                             const char *arc, int idx)
{
    char base[600], music_wav[700], part_wav[700];
    snprintf(base, sizeof(base), "%s_part%02d", cfg->out_prefix, idx + 1);
    snprintf(music_wav, sizeof(music_wav), "%s_music.wav", base);
    snprintf(part_wav, sizeof(part_wav), "%s.wav", base);

    RenderOpts o;
    render_opts_defaults(&o);
    o.fade_in_default = 0.5f;
    o.fade_out_default = 2.0f;
    o.narc = render_parse_arc_str(arc, o.arc, MAX_ARC);
    o.bpm_mode = st->bpm;
    o.snap = st->bpm;
    o.keylock = st->keylock;
    if (o.keylock) snprintf(o.keylock_target, sizeof(o.keylock_target), "auto");

    printf("[part %02d] pass A: music (%s engine %s)\n", idx + 1, st->name,
           cfg->engine == PLAN_ENGINE_OMICRON ? "omicron" : "rng");
    fflush(stdout);
    if (render_edl(music_edl, music_wav, &o)) return 1;

    char bed[1024], combined[16384];
    snprintf(bed, sizeof(bed), "in0 at0 v0 fin0.5 fout0.5 %s", music_wav);
    snprintf(combined, sizeof(combined), "%s,%s", bed, field_edl);

    printf("[part %02d] pass B: + field recordings\n", idx + 1);
    fflush(stdout);
    RenderOpts ob;
    render_opts_defaults(&ob);
    ob.fade_in_default = 0.5f;
    ob.fade_out_default = 2.0f;
    ob.master_mode = 1;
    snprintf(ob.master_graph, sizeof(ob.master_graph),
             "acompressor=threshold=-12dB:ratio=2:attack=30:release=300,alimiter=limit=0.7071");
    if (render_edl(combined, part_wav, &ob)) return 1;
    return 0;
}

int compose_run(ComposeCfg *cc)
{
    PlanCfg *cfg = &cc->plan;
    /* caller must have run plan_cfg_defaults() before filling fields */
    const StyleSpec *st = plan_style(cfg->style);
    compose_resolve_dirs(cc);

    if (!cfg->have_seed) {
        cfg->seed = (uint64_t)time(NULL) ^ ((uint64_t)getpid() << 32);
        if (cfg->seed == 0) cfg->seed = 1;
    }

    printf("=== gram ===\n");
    printf("style: %s | parts: %d x %.0f s | seed: %llu%s | engine: %s\n",
           st->name, cfg->parts, cfg->part_len, (unsigned long long)cfg->seed,
           cfg->have_seed ? "" : " (auto)",
           cfg->engine == PLAN_ENGINE_OMICRON ? "omicron" : "rng");
    printf("libraries:\n");

    TrackList mus = { 0 }, fld = { 0 };
    library_load(&mus, cfg->mus_dir, "music", cfg->max_files);
    library_load(&fld, cfg->fld_dir, "field", cfg->max_files);

    printf("analyzing (~/.cache/tj):\n");
    library_analyze(&mus, "music");
    library_analyze(&fld, "field");

    int roles[3] = { 0, 0, 0 };
    for (int i = 0; i < mus.n; i++) roles[mus.v[i].role]++;
    for (int i = 0; i < fld.n; i++) roles[fld.v[i].role]++;
    printf("texture roles: ambient=%d motion=%d pulse=%d\n", roles[0], roles[1], roles[2]);

    PlanResult res;
    plan_run(cfg, &mus, &fld, &res);

    for (int p = 0; p < cfg->parts; p++) {
        char path[760];
        snprintf(path, sizeof(path), "%s_part%02d_music.edl", cfg->out_prefix, p + 1);
        write_file(path, res.music_edls[p]);
        snprintf(path, sizeof(path), "%s_part%02d_field.edl", cfg->out_prefix, p + 1);
        write_file(path, res.field_edls[p]);
        snprintf(path, sizeof(path), "%s_part%02d.arc", cfg->out_prefix, p + 1);
        write_file(path, res.arcs[p]);
        if (res.vedls[p] && res.vedls[p][0]) {
            snprintf(path, sizeof(path), "%s_part%02d_music.vedl", cfg->out_prefix, p + 1);
            write_file(path, res.vedls[p]);
        }
    }

    if (cfg->dry_run) {
        printf("dry-run: plans written, no audio/video rendered.\n");
        plan_free(cfg, &res);
        return 0;
    }

    for (int p = 0; p < cfg->parts; p++) {
        if (render_part_audio(cfg, st, res.music_edls[p], res.field_edls[p], res.arcs[p], p))
            die("part %02d audio failed", p + 1);
        if (cc->av) {
            char part_wav[700], part_mp4[700];
            snprintf(part_wav, sizeof(part_wav), "%s_part%02d.wav", cfg->out_prefix, p + 1);
            snprintf(part_mp4, sizeof(part_mp4), "%s_part%02d.mp4", cfg->out_prefix, p + 1);
            AvOpts avo;
            av_opts_defaults(&avo);
            if (av_render(res.music_edls[p], res.vedls[p], res.arcs[p],
                          part_wav, cc->vid_dir, part_mp4, &avo))
                die("part %02d video failed", p + 1);
        }
    }

    /* concatenate */
    if (cc->av) {
        char mix[700], list_path[700];
        snprintf(mix, sizeof(mix), "%s_mix.mp4", cfg->out_prefix);
        snprintf(list_path, sizeof(list_path), "%s_concat.txt", cfg->out_prefix);
        FILE *lp = fopen(list_path, "w");
        if (!lp) die("cannot write %s", list_path);
        for (int i = 0; i < cfg->parts; i++)
            fprintf(lp, "file '%s_part%02d.mp4'\n", cfg->out_prefix, i + 1);
        fclose(lp);
        unlink(mix);
        if (cfg->parts > 1) {
            char cmd[4096], qlist[760];
            sh_quote(qlist, sizeof(qlist), list_path);
            snprintf(cmd, sizeof(cmd),
                     "ffmpeg -v error -y -f concat -safe 0 -i %s -c copy %s", qlist, mix);
            if (system(cmd) != 0) die("concat failed");
        } else {
            char cmd[2048];
            snprintf(cmd, sizeof(cmd), "cp \"%s_part01.mp4\" \"%s\"", cfg->out_prefix, mix);
            if (system(cmd) != 0) die("copy failed");
        }
        unlink(list_path);
        printf("Done! Output: %s\nReproduce with: gram compose %s %llu --engine %s\n",
               mix, st->name, (unsigned long long)cfg->seed,
               cfg->engine == PLAN_ENGINE_OMICRON ? "omicron" : "rng");
    } else {
        printf("Done! Parts: %s_partNN.wav\nReproduce with: gram compose %s %llu --engine %s\n",
               cfg->out_prefix, st->name, (unsigned long long)cfg->seed,
               cfg->engine == PLAN_ENGINE_OMICRON ? "omicron" : "rng");
    }

    plan_free(cfg, &res);
    free(mus.v);
    free(fld.v);
    return 0;
}
