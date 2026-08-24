#include "av_render.h"

#include "analysis.h"
#include "util.h"
#include "visual.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PHI 0.6180339887498949

void av_opts_defaults(AvOpts *o)
{
    o->w = 932;   /* PAL-height raster, wide */
    o->h = 576;
    o->fps = 25;
}

typedef struct {
    TrackSpec spec;
    char op;
    char gen[8];
    double at, span;
    int order;

    int active;
    int16_t *pcm;
    uint32_t pcm_frames;
    FILE *vpipe;
    Scope *scope;
    unsigned seed;
    const char *clip;
} VEntry;

typedef struct {
    const char **v;
    int n, cap;
} StrList;

static void strl_push(StrList *l, const char *s)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 32;
        l->v = xrealloc(l->v, (size_t)l->cap * sizeof(char *));
    }
    l->v[l->n++] = s;
}

static void scan_video_dir(const char *dir, StrList *out)
{
    struct dirent **ents = NULL;
    int cnt = scandir(dir, &ents, NULL, alphasort);
    if (cnt < 0) return;
    static const char *exts[] = { ".mp4", ".mov", ".mkv", ".avi", ".webm", ".m4v" };
    for (int i = 0; i < cnt; i++) {
        const char *name = ents[i]->d_name;
        if (name[0] != '.') {
            size_t len = strlen(name);
            for (size_t k = 0; k < sizeof(exts) / sizeof(exts[0]); k++) {
                size_t el = strlen(exts[k]);
                if (len > el && strcasecmp(name + len - el, exts[k]) == 0) {
                    char path[1024];
                    snprintf(path, sizeof(path), "%s/%s", dir, name);
                    strl_push(out, xstrdup(path));
                    break;
                }
            }
        }
        free(ents[i]);
    }
    free(ents);
}

static void parse_vedl(const char *vedl, VEntry *entries, int n)
{
    if (!vedl || !vedl[0]) return;
    char *buf = xmalloc(strlen(vedl) + 1);
    strcpy(buf, vedl);
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        int idx;
        char op, gen[16];
        if (sscanf(line, "%d %c %15s", &idx, &op, gen) != 3) continue;
        if (idx < 1 || idx > n) continue;
        VEntry *e = &entries[idx - 1]; /* vedl indices are 1-based */
        e->op = op;
        snprintf(e->gen, sizeof(e->gen), "%.7s",
                 strcmp(gen, "file") == 0 ? "file" : gen);
    }
    free(buf);
}

static void entry_start(VEntry *e, const AvOpts *o)
{
    e->active = 1;
    if (strcmp(e->gen, "file") == 0 && !e->clip)
        snprintf(e->gen, sizeof(e->gen), "scope");
    /* 'vfile' streams spec.filepath itself (deterministic text edits),
     * honoring the EDL in-point; 'file' pairs into the pool by hash */
    int is_vfile = strcmp(e->gen, "vfile") == 0;
    int is_file = strcmp(e->gen, "file") == 0;

    if (is_file || is_vfile) {
        const char *src = is_vfile ? e->spec.filepath : e->clip;
        double cdur = source_duration_sec(src);
        if (cdur <= 0) cdur = 60.0;
        double span = e->span < cdur * 0.9 ? e->span : cdur * 0.9;
        double off;
        if (is_vfile) {
            off = e->spec.has_in ? (double)e->spec.in_sec : 0.0;
            if (off + span > cdur) off = cdur > span ? cdur - span : 0.0;
        } else {
            double frac = fmod((double)(fnv1a(e->spec.filepath) % 1000003u) * PHI, 1.0);
            off = frac * (cdur > span ? cdur - span : 0.0);
        }
        char cmd[2048];
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -v quiet -ss %.3f -t %.3f -i \"%s\" -map 0:v:0 "
                 "-vf fps=%d,scale=%d:%d -f rawvideo -pix_fmt rgb24 -",
                 off, e->span, src, o->fps, o->w, o->h);
        e->vpipe = popen(cmd, "r");
        if (!e->vpipe) snprintf(e->gen, sizeof(e->gen), "scope");
        return;
    }

    if (!e->pcm && strcmp(e->gen, "glyph") != 0 &&
        strcmp(e->gen, "vfile") != 0) {
        e->pcm = load_audio_slice(&e->spec, &e->pcm_frames, 1.0f, 1.0f);
        if (!e->pcm) snprintf(e->gen, sizeof(e->gen), "glyph");
    }
    if (strcmp(e->gen, "scope") == 0) {
        e->scope = scope_new(o->w, o->h);
        if (!e->scope) snprintf(e->gen, sizeof(e->gen), "wave");
    }
}

static void entry_stop(VEntry *e)
{
    if (!e->active) return;
    e->active = 0;
    if (e->vpipe) { pclose(e->vpipe); e->vpipe = NULL; }
    free(e->pcm);
    e->pcm = NULL;
    e->pcm_frames = 0;
    scope_free(e->scope);
    e->scope = NULL;
}

static int cmp_entry_ptr(const void *a, const void *b)
{
    const VEntry *x = *(const VEntry *const *)a;
    const VEntry *y = *(const VEntry *const *)b;
    if (x->at < y->at) return -1;
    if (x->at > y->at) return 1;
    return x->order - y->order;
}

static float arc_gain_at(const ArcPoint *arc, int narc, float t)
{
    if (narc <= 0) return 1.0f;
    if (t <= arc[0].t) return arc[0].g;
    for (int i = 0; i + 1 < narc; i++) {
        if (t >= arc[i].t && t <= arc[i + 1].t) {
            float span = arc[i + 1].t - arc[i].t;
            float fr = span > 0 ? (t - arc[i].t) / span : 0.0f;
            return arc[i].g + fr * (arc[i + 1].g - arc[i].g);
        }
    }
    return arc[narc - 1].g;
}

int av_render(const char *music_edl, const char *vedl, const char *arc_str,
              const char *audio_wav, const char *vid_dir, const char *out_mp4,
              const AvOpts *opts)
{
    AvOpts def;
    if (!opts) { av_opts_defaults(&def); opts = &def; }
    int W = opts->w, H = opts->h, FPS = opts->fps;

    char *edl_buf = xmalloc(strlen(music_edl) + 1);
    strcpy(edl_buf, music_edl);

    VEntry *entries = NULL;
    int cap = 0, n = 0;
    char *tok = strtok(edl_buf, ",");
    while (tok) {
        if (n == cap) {
            cap = cap ? cap * 2 : 256;
            entries = xrealloc(entries, sizeof(VEntry) * (size_t)cap);
            memset(entries + n, 0, sizeof(VEntry) * (size_t)(cap - n));
        }
        parse_track_spec(tok, &entries[n].spec);
        entries[n].op = '+';
        snprintf(entries[n].gen, sizeof(entries[n].gen), "wave");
        entries[n].seed = (unsigned)(fnv1a(entries[n].spec.filepath) ^ (unsigned)(n * 2654435761u));
        n++;
        tok = strtok(NULL, ",");
    }
    if (n == 0) die("av_render: empty EDL");

    parse_vedl(vedl, entries, n);

    double total_dur = 0.0;
    for (int i = 0; i < n; i++) {
        VEntry *e = &entries[i];
        e->order = i;
        e->at = e->spec.has_at ? e->spec.at_sec : 0.0;
        if (e->spec.has_in && e->spec.has_out)
            e->span = e->spec.out_sec - e->spec.in_sec;
        else
            e->span = source_duration_sec(e->spec.filepath);
        if (e->span <= 0 || e->span > 3600) e->span = 30.0;
        if (e->at + e->span > total_dur) total_dur = e->at + e->span;
    }

    /* the muxed audio can outlast the music EDL (field layering adds
     * tail); stretch the longest-running entry so visuals cover it all.
     * silent renders (audio_wav == NULL) keep the EDL's own duration. */
    double audio_dur = audio_wav && audio_wav[0] ? source_duration_sec(audio_wav) : 0.0;
    if (audio_dur > total_dur) {
        int last = 0;
        for (int i = 1; i < n; i++)
            if (entries[i].at + entries[i].span > entries[last].at + entries[last].span)
                last = i;
        entries[last].span = audio_dur - entries[last].at;
        total_dur = audio_dur;
    }

    StrList pool = { 0 };
    int want_pool = 0;
    for (int i = 0; i < n; i++)
        if (strcmp(entries[i].gen, "file") == 0) { want_pool = 1; break; }
    if (!want_pool) {
        /* vfile-only edit: every entry streams its own spec.filepath */
    } else if (vid_dir && vid_dir[0]) {
        scan_video_dir(vid_dir, &pool);
    }
    if (want_pool && pool.n > 0) {
        printf("av: video pool: %d clips (%s)\n", pool.n, vid_dir);
        for (int i = 0; i < n; i++)
            if (strcmp(entries[i].gen, "file") == 0)
                entries[i].clip = pool.v[fnv1a(entries[i].spec.filepath) % (unsigned long)pool.n];
    } else {
        if (want_pool) printf("av: no video pool, procedural visuals only\n");
        for (int i = 0; i < n; i++)
            if (strcmp(entries[i].gen, "file") == 0)
                snprintf(entries[i].gen, sizeof(entries[i].gen),
                         entries[i].op == '-' ? "wave" : "scope");
    }

    ArcPoint arc[MAX_ARC];
    int narc = arc_str ? render_parse_arc_str(arc_str, arc, MAX_ARC) : 0;

    long long nframes = (long long)(total_dur * FPS + 0.5);

    char outcmd[4096];
    if (audio_wav && audio_wav[0])
        snprintf(outcmd, sizeof(outcmd),
                 "ffmpeg -v warning -y -f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i - "
                 "-i \"%s\" -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p "
                 "-c:a aac -b:a 192k -shortest \"%s\"",
                 W, H, FPS, audio_wav, out_mp4);
    else
        snprintf(outcmd, sizeof(outcmd),
                 "ffmpeg -v warning -y -f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i - "
                 "-c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p -an \"%s\"",
                 W, H, FPS, out_mp4);
    FILE *enc = popen(outcmd, "w");
    if (!enc) die("av_render: cannot start ffmpeg encoder");

    Frame *master = frame_new(W, H);
    Frame *src = frame_new(W, H);

    VEntry **sorted = xmalloc(sizeof(VEntry *) * (size_t)n);
    for (int i = 0; i < n; i++) sorted[i] = &entries[i];
    qsort(sorted, (size_t)n, sizeof(VEntry *), cmp_entry_ptr);

    /* active-window scheduler: layers come off the at-sorted array through
     * a monotonic cursor, ended layers are filtered out stably. Per-frame
     * cost is proportional to concurrent layers, not to the cut count,
     * which keeps huge text edits feasible. Blend order equals the old
     * full-scan order: sorted by (at, index) among started layers. */
    VEntry **act = xmalloc(sizeof(VEntry *) * (size_t)n);
    int nact = 0;
    long long next_start = 0;

    for (long long fidx = 0; fidx < nframes; fidx++) {
        double t = (double)fidx / FPS;

        while (next_start < n && sorted[next_start]->at <= t) {
            VEntry *e = sorted[next_start++];
            if (t < e->at + e->span && !e->active) {
                entry_start(e, opts);
                act[nact++] = e;
            }
        }
        int w = 0;
        for (int i = 0; i < nact; i++) {
            VEntry *e = act[i];
            if (t >= e->at + e->span)
                entry_stop(e);
            else
                act[w++] = e;
        }
        nact = w;

        frame_clear(master, 2, 2, 4);

        for (int i = 0; i < nact; i++) {
            VEntry *e = act[i];
            double local = t - e->at;

            if (e->vpipe) {
                size_t want = (size_t)W * H * 3;
                size_t got = fread(src->px, 1, want, e->vpipe);
                if (got == 0) {
                    pclose(e->vpipe);
                    e->vpipe = NULL; /* hold last decoded frame in src */
                }
            } else if (!e->vpipe && e->active &&
                       (strcmp(e->gen, "file") == 0 || strcmp(e->gen, "vfile") == 0)) {
                /* pipe exhausted: freeze on last frame */
            } else if (strcmp(e->gen, "scope") == 0 && e->pcm) {
                scope_render(e->scope, src, e->pcm, e->pcm_frames / 2,
                             TARGET_SAMPLE_RATE, local, 0.03);
            } else if (strcmp(e->gen, "wave") == 0 && e->pcm) {
                wave_render(src, e->pcm, e->pcm_frames / 2,
                            TARGET_SAMPLE_RATE, local, 0.5, 120, 220, 160);
            } else {
                glyph_render(src, e->seed);
            }

            float fin_s = e->spec.has_fin ? e->spec.fin_sec : 1.2f;
            float fout_s = e->spec.has_fout ? e->spec.fout_sec : 2.0f;
            float a = 1.0f;
            if (fin_s > 0 && local < fin_s)
                a = sinf((float)(local / fin_s) * (float)M_PI / 2.0f);
            double rem = e->span - local;
            if (fout_s > 0 && rem < fout_s) {
                float fo = sinf((float)(rem / fout_s) * (float)M_PI / 2.0f);
                if (fo < a) a = fo;
            }
            frame_blend(master, src, e->op, a);
        }

        frame_expose(master, arc_gain_at(arc, narc, (float)t));

        if (nframes > 16) {
            if (fidx < 8) frame_expose(master, (float)(fidx + 1) / 8.0f);
            else if (fidx >= nframes - 8) frame_expose(master, (float)(nframes - fidx) / 8.0f);
        }

        fwrite(master->px, 1, (size_t)W * H * 3, enc);
        if (fidx % FPS == 0) {
            printf("\rav: frame %lld/%lld (%.0f%%)", fidx + 1, nframes,
                   100.0 * (double)(fidx + 1) / (double)nframes);
            fflush(stdout);
        }
    }
    printf("\n");

    int rc = pclose(enc);
    frame_free(master);
    frame_free(src);
    for (int i = 0; i < n; i++) entry_stop(&entries[i]);
    free(entries);
    free(sorted);
    free(act);
    free(edl_buf);
    if (rc != 0) fprintf(stderr, "gram av: encoder exited %d\n", rc);
    printf("av rendered -> %s\n", out_mp4);
    return rc != 0;
}
