#include "edit.h"

#include "analysis.h"
#include "av_render.h"
#include "render.h"
#include "util.h"

#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PHI 0.6180339887498949

void edit_cfg_defaults(EditCfg *cfg)
{
    cfg->span = EDIT_DEFAULT_SPAN;
}

/* ------------------------------------------------------------------ */
/* pure planner                                                        */

static int fold_letter(int c)
{
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (c < 'a' || c > 'z') return 0;
    return c - 'a' + 1;
}

long edit_plan_text(const char *text, char const *const *paths,
                    const double *durs, int n, const EditCfg *cfg,
                    EditCut *out, int out_cap)
{
    (void)paths;
    if (!text || n <= 0 || !cfg || cfg->span <= 0.0) return 0;
    double cursor = 0.0;
    long placed = 0;
    long k = 0; /* position among letters */
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') continue;
        int v = fold_letter(*p);
        if (!v) continue;
        if (placed >= out_cap) return -1;

        int clip = (v - 1) % n;
        double dur = durs ? durs[clip] : 0.0;
        double span = cfg->span;
        if (dur > 0.0 && span > dur * 0.9) span = dur * 0.9;
        double room = dur > span ? dur - span : 0.0;
        double in = fmod((double)k * PHI, 1.0) * room;

        out[placed].clip = clip;
        out[placed].in_sec = in;
        out[placed].span = span;
        out[placed].at = cursor;
        cursor += span;
        placed++;
        k++;
    }
    return placed;
}

/* ------------------------------------------------------------------ */
/* string buffer                                                       */

typedef struct {
    char *buf;
    size_t len, cap;
} Sb;

static void sb_init(Sb *s)
{
    s->cap = 4096;
    s->buf = xmalloc(s->cap);
    s->buf[0] = 0;
    s->len = 0;
}

static void sb_put(Sb *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (w < 0) return;
    size_t need = s->len + (size_t)w + 1;
    if (need > s->cap) {
        while (s->cap < need) s->cap *= 2;
        s->buf = xrealloc(s->buf, s->cap);
    }
    va_start(ap, fmt);
    vsnprintf(s->buf + s->len, s->cap - s->len, fmt, ap);
    va_end(ap);
    s->len += (size_t)w;
}

/* ------------------------------------------------------------------ */
/* io wrapper                                                          */

static void scan_video_pool(const char *dir, char ***paths, double **durs,
                            int *n, int max_files)
{
    struct dirent **ents = NULL;
    int cnt = scandir(dir, &ents, NULL, alphasort);
    if (cnt < 0) die("edit: cannot scan video dir %s", dir);
    static const char *exts[] = { ".mp4", ".mov", ".mkv", ".avi", ".webm", ".m4v" };
    for (int i = 0; i < cnt; i++) {
        const char *name = ents[i]->d_name;
        if (name[0] != '.') {
            size_t len = strlen(name);
            for (size_t e = 0; e < sizeof(exts) / sizeof(exts[0]); e++) {
                size_t el = strlen(exts[e]);
                if (len > el && strcasecmp(name + len - el, exts[e]) == 0) {
                    char path[1024];
                    snprintf(path, sizeof(path), "%s/%s", dir, name);
                    if (*n >= max_files) break;
                    *paths = xrealloc(*paths, sizeof(char *) * (size_t)(*n + 1));
                    *durs = xrealloc(*durs, sizeof(double) * (size_t)(*n + 1));
                    (*paths)[*n] = xstrdup(path);
                    (*durs)[*n] = source_duration_sec(path);
                    (*n)++;
                    break;
                }
            }
        }
        free(ents[i]);
    }
    free(ents);
    /* scandir+alphasort already yields name order; enforce it in case a
     * platform's scandir does not (determinism depends on it) */
    for (int i = 1; i < *n; i++) {
        int j = i;
        while (j > 0 && strcmp((*paths)[j - 1], (*paths)[j]) > 0) {
            char *tp = (*paths)[j - 1];
            double td = (*durs)[j - 1];
            (*paths)[j - 1] = (*paths)[j];
            (*durs)[j - 1] = (*durs)[j];
            (*paths)[j] = tp;
            (*durs)[j] = td;
            j--;
        }
    }
}

static char *read_stdin_all(void)
{
    size_t cap = 65536, len = 0;
    char *buf = xmalloc(cap);
    for (;;) {
        if (len + 4096 + 1 > cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
        size_t got = fread(buf + len, 1, 4096, stdin);
        len += got;
        if (got == 0) break;
    }
    buf[len] = 0;
    return buf;
}

int edit_run(const char *vid_dir, const EditCfg *cfg, const char *out_mp4,
             int w, int h, int fps, int max_files, const char *edl_dump)
{
    if (!out_mp4 || !out_mp4[0]) die("edit: output .mp4 path required");
    if (!vid_dir || !vid_dir[0]) die("edit: no video dir (use --vid)");

    char **paths = NULL;
    double *durs = NULL;
    int n = 0;
    printf("edit: scanning %s\n", vid_dir);
    scan_video_pool(vid_dir, &paths, &durs, &n, max_files > 0 ? max_files : 1000000);
    if (n == 0) die("edit: no video sources found in %s", vid_dir);
    printf("edit: pool of %d clip(s)\n", n);

    char *text = read_stdin_all();

    EditCut cuts[MAX_TRACKS];
    long ncuts = edit_plan_text(text, (char const *const *)paths, durs, n,
                                cfg, cuts, MAX_TRACKS);
    free(text);
    if (ncuts < 0)
        die("edit: text yields more than %d cuts (max per render)", MAX_TRACKS);
    if (ncuts == 0) die("edit: no letters a..z found in input");

    Sb edl, vedl;
    sb_init(&edl);
    sb_init(&vedl);
    for (long i = 0; i < ncuts; i++) {
        const EditCut *c = &cuts[i];
        sb_put(&edl, "%sin%.3f out%.3f at%.3f v0 fin%.2f fout%.2f %s",
               i ? ", " : "", c->in_sec, c->in_sec + c->span, c->at,
               0.0, 0.0, paths[c->clip]);
        sb_put(&vedl, "%ld + vfile\n", i + 1);
    }

    if (edl_dump && edl_dump[0]) {
        FILE *fp = fopen(edl_dump, "w");
        if (!fp) die("edit: cannot write %s", edl_dump);
        fprintf(fp, "%s\n", edl.buf);
        fclose(fp);
        printf("edit: edl dumped -> %s\n", edl_dump);
    }

    AvOpts o;
    av_opts_defaults(&o);
    if (w > 0) o.w = w;
    if (h > 0) o.h = h;
    if (fps > 0) o.fps = fps;

    printf("edit: rendering %ld cut(s) -> %s (%dx%d@%d)\n",
           ncuts, out_mp4, o.w, o.h, o.fps);
    /* silent mp4: no audio wav, arc unused; clips stream directly from
     * their own paths through the 'vfile' vedl generator */
    int rc = av_render(edl.buf, vedl.buf, NULL, NULL, NULL, out_mp4, &o);

    for (int i = 0; i < n; i++) free(paths[i]);
    free(paths);
    free(durs);
    free(edl.buf);
    free(vedl.buf);
    return rc;
}
