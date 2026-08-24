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

/* ------------------------------------------------------------------ */
/* original-audio mixdown                                              */
/*
 * Each cut carries its source clip's own sound. Clips are decoded once
 * (s16 stereo @48k) and cached under a byte budget; the mix is streamed
 * chronologically — cuts are already in timeline order — so memory stays
 * independent of the text length. Long mixes use Sony Wave64 (W64),
 * which has no 4 GB ceiling.
 */

#define EDIT_AUDIO_BUDGET (768ull << 20)

typedef struct {
    char *path;
    int16_t *pcm;      /* whole clip, interleaved s16 stereo */
    uint64_t samples;  /* individual s16 values (frames * channels) */
} ClipAudio;

typedef struct {
    ClipAudio *v;
    int n, cap;
    uint64_t bytes;
} AudioCache;

static ClipAudio *clip_audio(AudioCache *c, const char *path)
{
    for (int i = 0; i < c->n; i++)
        if (strcmp(c->v[i].path, path) == 0) return &c->v[i];

    TrackSpec spec;
    memset(&spec, 0, sizeof(spec));
    snprintf(spec.filepath, sizeof(spec.filepath), "%s", path);
    uint32_t samples = 0;
    int16_t *pcm = load_audio_slice(&spec, &samples, 1.0f, 1.0f);
    if (!pcm || samples == 0) { free(pcm); return NULL; }

    uint64_t bytes = (uint64_t)samples * sizeof(int16_t);
    while (c->n > 0 && c->bytes + bytes > EDIT_AUDIO_BUDGET) {
        free(c->v[0].path);
        free(c->v[0].pcm);
        c->bytes -= c->v[0].samples * sizeof(int16_t);
        memmove(&c->v[0], &c->v[1], sizeof(ClipAudio) * (size_t)(c->n - 1));
        c->n--;
    }
    if (c->n == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 16;
        c->v = xrealloc(c->v, sizeof(ClipAudio) * (size_t)c->cap);
    }
    ClipAudio *ca = &c->v[c->n++];
    ca->path = xstrdup(path);
    ca->pcm = pcm;
    ca->samples = samples;
    c->bytes += bytes;
    return ca;
}

static void put_u16(FILE *fp, unsigned v)
{
    fputc(v & 0xff, fp);
    fputc((v >> 8) & 0xff, fp);
}

static void put_u32(FILE *fp, uint32_t v)
{
    put_u16(fp, v & 0xffff);
    put_u16(fp, v >> 16);
}

static void put_u64(FILE *fp, uint64_t v)
{
    put_u32(fp, (uint32_t)(v & 0xffffffffu));
    put_u32(fp, (uint32_t)(v >> 32));
}

/* Sony Wave64 GUIDs (little-endian serialization, as in ffmpeg's w64 demuxer) */
static const unsigned char GUID_RIFF[] = {
    'r','i','f','f', 0x2e,0x91,0xcf,0x11,0xa5,0xd6,0x28,0xdb,0x04,0xc1,0x00,0x00
};
static const unsigned char GUID_WAVE[] = {
    'w','a','v','e', 0xf3,0xac,0xd3,0x11,0x8c,0xd1,0x00,0xc0,0x4f,0x8e,0xdb,0x8a
};
static const unsigned char GUID_FMT[] = {
    'f','m','t',' ', 0xf3,0xac,0xd3,0x11,0x8c,0xd1,0x00,0xc0,0x4f,0x8e,0xdb,0x8a
};
static const unsigned char GUID_DATA[] = {
    'd','a','t','a', 0xe1,0x56,0xd4,0x11,0x82,0x92,0x44,0xd4,0xac,0x12,0x3f,0x09,0x00,0x00
};

typedef struct {
    FILE *fp;
    int w64;
    const char *path;
} MixFile;

static void mix_begin(MixFile *m, const char *path, int w64)
{
    m->fp = fopen(path, "wb");
    m->w64 = w64;
    m->path = path;
    if (!m->fp) die("edit: cannot write %s", path);
    setvbuf(m->fp, NULL, _IOFBF, 1 << 20);
    if (w64) {
        /* chunk sizes include their own 24-byte header (guid + size) */
        fwrite(GUID_RIFF, 1, 16, m->fp);
        put_u64(m->fp, 0);              /* offset 16: total file size */
        fwrite(GUID_WAVE, 1, 16, m->fp);
        fwrite(GUID_FMT, 1, 16, m->fp);
        put_u64(m->fp, 40);             /* 24 header + 16 pcm-fmt bytes */
        put_u16(m->fp, 1);              /* PCM */
        put_u16(m->fp, TARGET_CHANNELS);
        put_u32(m->fp, TARGET_SAMPLE_RATE);
        put_u32(m->fp, TARGET_SAMPLE_RATE * TARGET_CHANNELS * 2);
        put_u16(m->fp, TARGET_CHANNELS * 2);
        put_u16(m->fp, 16);
        fwrite(GUID_DATA, 1, 16, m->fp);
        put_u64(m->fp, 24);             /* patched at end */
    } else {
        fwrite("RIFF", 1, 4, m->fp);
        put_u32(m->fp, 0xffffffffu);    /* patched at end */
        fwrite("WAVEfmt ", 1, 8, m->fp);
        put_u32(m->fp, 16);
        put_u16(m->fp, 1);              /* PCM */
        put_u16(m->fp, TARGET_CHANNELS);
        put_u32(m->fp, TARGET_SAMPLE_RATE);
        put_u32(m->fp, TARGET_SAMPLE_RATE * TARGET_CHANNELS * 2);
        put_u16(m->fp, TARGET_CHANNELS * 2);
        put_u16(m->fp, 16);
        fwrite("data", 1, 4, m->fp);
        put_u32(m->fp, 0xffffffffu);    /* patched at end */
    }
}

static void mix_finish(MixFile *m)
{
    long end = ftell(m->fp);
    if (m->w64) {
        fseek(m->fp, 16, SEEK_SET);
        put_u64(m->fp, (uint64_t)end);
        fseek(m->fp, 96, SEEK_SET);
        put_u64(m->fp, (uint64_t)end - 80); /* data chunk: 24 header + payload */
    } else {
        uint64_t data = (uint64_t)end - 44;
        fseek(m->fp, 4, SEEK_SET);
        put_u32(m->fp, (uint32_t)(data + 36));
        fseek(m->fp, 40, SEEK_SET);
        put_u32(m->fp, (uint32_t)data);
    }
    fclose(m->fp);
    printf("edit: audio mixed -> %s\n", m->path);
}

/* stream every cut's slice from its clip's own PCM, chronological order */
static void edit_build_mix(const EditCut *cuts, long ncuts,
                           char const *const *paths, AudioCache *cache,
                           const char *out_mp4, char *path_out, size_t path_cap)
{
    /* <out>_audio.wav / .w64 next to the mp4 (gram av sidecar convention) */
    char base[1024];
    snprintf(base, sizeof(base), "%s", out_mp4);
    size_t bl = strlen(base);
    if (bl > 4 && strcmp(base + bl - 4, ".mp4") == 0) base[bl - 4] = 0;

    uint64_t est_bytes = 0;
    for (long i = 0; i < ncuts; i++)
        est_bytes += (uint64_t)(cuts[i].span * TARGET_SAMPLE_RATE + 0.5) * 4;
    int w64 = est_bytes >= 3500000000ull;

    MixFile m;
    char path[1100];
    snprintf(path, sizeof(path), "%s_audio.%s", base, w64 ? "w64" : "wav");
    snprintf(path_out, path_cap, "%s", path);
    mix_begin(&m, path, w64);

    static int16_t zero[2 * 4800]; /* silence padding, 0.05s per write */
    memset(zero, 0, sizeof(zero));

    for (long i = 0; i < ncuts; i++) {
        const EditCut *cut = &cuts[i];
        uint64_t frames = (uint64_t)((double)cut->span * TARGET_SAMPLE_RATE + 0.5);
        ClipAudio *ca = clip_audio(cache, paths[cut->clip]);
        uint64_t done = 0;
        if (ca && ca->samples >= (uint64_t)TARGET_CHANNELS) {
            uint64_t src_frame = (uint64_t)((double)cut->in_sec * TARGET_SAMPLE_RATE + 0.5);
            uint64_t clip_frames = ca->samples / TARGET_CHANNELS;
            if (src_frame < clip_frames) {
                uint64_t avail = clip_frames - src_frame;
                uint64_t copy = frames < avail ? frames : avail;
                fwrite(ca->pcm + src_frame * TARGET_CHANNELS,
                       sizeof(int16_t) * TARGET_CHANNELS, copy, m.fp);
                done = copy;
            }
        }
        while (done < frames) {
            uint64_t chunk = frames - done;
            if (chunk > sizeof(zero) / sizeof(zero[0]) / TARGET_CHANNELS)
                chunk = sizeof(zero) / sizeof(zero[0]) / TARGET_CHANNELS;
            fwrite(zero, sizeof(int16_t) * TARGET_CHANNELS, chunk, m.fp);
            done += chunk;
        }
    }
    mix_finish(&m);
}


int edit_run(const char *vid_dir, const EditCfg *cfg, const char *out_mp4,
             int w, int h, int fps, int max_files, const char *edl_dump,
             int mute)
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

    /* pre-count letters so the cut table can live on the heap: texts are
     * unbounded and one letter always yields exactly one cut */
    long nletters = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++)
        if (fold_letter(*p)) nletters++;
    EditCut *cuts = xmalloc(sizeof(EditCut) * (size_t)(nletters > 0 ? nletters : 1));
    long ncuts = edit_plan_text(text, (char const *const *)paths, durs, n,
                                cfg, cuts, nletters);
    free(text);
    if (ncuts != nletters) die("edit: planning failed (%ld/%ld)", ncuts, nletters);
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

    /* original clip audio: decode each used clip once, stream the slices
     * chronologically into a wav sidecar; --mute falls back to -an */
    const char *audio_wav = NULL;
    AudioCache cache = { 0 };
    char audio_path[1100];
    if (!mute) {
        printf("edit: mixing original clip audio\n");
        edit_build_mix(cuts, ncuts, (char const *const *)paths, &cache,
                       out_mp4, audio_path, sizeof(audio_path));
        audio_wav = audio_path;
    }

    printf("edit: rendering %ld cut(s) -> %s (%dx%d@%d)\n",
           ncuts, out_mp4, o.w, o.h, o.fps);
    /* clips stream directly from their own paths through 'vfile' */
    int rc = av_render(edl.buf, vedl.buf, NULL, audio_wav, NULL, out_mp4, &o);

    for (int i = 0; i < cache.n; i++) {
        free(cache.v[i].path);
        free(cache.v[i].pcm);
    }
    free(cache.v);
    for (int i = 0; i < n; i++) free(paths[i]);
    free(paths);
    free(durs);
    free(edl.buf);
    free(vedl.buf);
    return rc;
}
