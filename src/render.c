#include "render.h"

#include "analysis.h"
#include "util.h"

#include <ctype.h>
#include <glob.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define FADE_IN_SECONDS 1.2f
#define FADE_OUT_SECONDS 15.0f

typedef struct {
    char riff[4];
    uint32_t overall_size;
    char wave[4];
    char fmt_chunk_marker[4];
    uint32_t length_of_fmt;
    uint16_t format_type;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byterate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data_chunk_id[4];
    uint32_t data_size;
} WavHeader;

/* TPDF dither (1 LSB) to avoid quantization distortion in float->int16 */
static uint32_t dither_state = 0x12345678u;
static float tpdf_dither(void)
{
    dither_state = dither_state * 1664525u + 1013904223u;
    float a = ((dither_state >> 8) & 0xFFFF) * (1.0f / 65535.0f);
    dither_state = dither_state * 1664525u + 1013904223u;
    float b = ((dither_state >> 8) & 0xFFFF) * (1.0f / 65535.0f);
    return a - b;
}

int write_wav(const char *filename, const float *data, uint32_t num_samples, float scale)
{
    FILE *fp = fopen(filename, "wb");
    if (!fp) return 0;

    uint32_t data_size = num_samples * sizeof(int16_t);
    WavHeader header;
    memcpy(header.riff, "RIFF", 4);
    header.overall_size = data_size + 36;
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt_chunk_marker, "fmt ", 4);
    header.length_of_fmt = 16;
    header.format_type = 1; /* PCM */
    header.channels = TARGET_CHANNELS;
    header.sample_rate = TARGET_SAMPLE_RATE;
    header.bits_per_sample = 16;
    header.block_align = (TARGET_CHANNELS * 16) / 8;
    header.byterate = TARGET_SAMPLE_RATE * header.block_align;
    memcpy(header.data_chunk_id, "data", 4);
    header.data_size = data_size;

    fwrite(&header, sizeof(WavHeader), 1, fp);
    for (uint32_t i = 0; i < num_samples; i++) {
        float v = data[i] * scale + tpdf_dither();
        if (v > 32767.0f) v = 32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        int16_t s = (int16_t)lrintf(v);
        fwrite(&s, sizeof(int16_t), 1, fp);
    }
    fclose(fp);
    return 1;
}

void brickwall_limit(const char *path, float limit)
{
    FILE *fp = fopen(path, "r+b");
    if (!fp) return;
    uint32_t rate;
    uint16_t block_align;
    long data_pos;
    uint32_t data_size;
    if (wav_data_info(fp, &rate, &block_align, &data_pos, &data_size) != 0) {
        fclose(fp);
        return;
    }
    if (fseek(fp, data_pos, SEEK_SET) != 0) { fclose(fp); return; }
    int16_t *s = malloc(data_size);
    if (!s) { fclose(fp); return; }
    size_t got = fread(s, 1, data_size, fp);
    size_t ns = got / sizeof(int16_t);
    int16_t lim = (int16_t)(limit * 32767.0f);
    int clipped = 0;
    for (size_t i = 0; i < ns; i++) {
        if (s[i] > lim) { s[i] = lim; clipped++; }
        else if (s[i] < -lim) { s[i] = (int16_t)(-lim); clipped++; }
    }
    if (clipped && fseek(fp, data_pos, SEEK_SET) == 0)
        fwrite(s, 1, got, fp);
    free(s);
    fclose(fp);
    if (clipped)
        fprintf(stderr, "  peak-limited %d samples to %.1f dBFS\n",
                clipped, 20.0f * log10f(limit));
}

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static float median_bpm(float *arr, int n)
{
    if (n <= 0) return 0.0f;
    qsort(arr, (size_t)n, sizeof(float), cmp_float);
    if (n % 2 == 1) return arr[n / 2];
    return (arr[n / 2 - 1] + arr[n / 2]) / 2.0f;
}

/* snap a time to the nearest beat at the given BPM */
static float snap_beat(float sec, float bpm)
{
    if (bpm <= 0.0f) return sec;
    float beat = 60.0f / bpm;
    return roundf(sec / beat) * beat;
}

/* build an ffmpeg atempo filter chain for a tempo factor */
static void tempo_filter(char *filt, size_t n, float factor)
{
    if (factor <= 0.0f) { snprintf(filt, n, "atempo=1"); return; }
    char *p = filt;
    size_t left = n;
    int first = 1;
    while (factor > 2.0f && left > 16) {
        if (!first) *p++ = ',';
        first = 0;
        int w = snprintf(p, left, "atempo=2.0");
        if (w < 0 || (size_t)w >= left) return;
        p += w; left -= (size_t)w;
        factor /= 2.0f;
    }
    while (factor < 0.5f && left > 16) {
        if (!first) *p++ = ',';
        first = 0;
        int w = snprintf(p, left, "atempo=0.5");
        if (w < 0 || (size_t)w >= left) return;
        p += w; left -= (size_t)w;
        factor /= 0.5f;
    }
    if (!first && left > 1) { *p++ = ','; left--; }
    snprintf(p, left, "atempo=%.6g", factor);
}

/* parse a single EDL token like "in10out50 at2 input.mp3" */
void parse_track_spec(const char *str, TrackSpec *spec)
{
    memset(spec, 0, sizeof(TrackSpec));
    spec->vol = 1.0f;
    char buffer[2048];
    strncpy(buffer, str, sizeof(buffer));
    buffer[sizeof(buffer) - 1] = '\0';

    char *p = buffer;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        if (strncmp(p, "fin", 3) == 0 && (isdigit((unsigned char)p[3]) || p[3] == '.')) {
            spec->fin_sec = atof(p + 3);
            spec->has_fin = 1;
            while (*p && !isspace((unsigned char)*p)) p++;
        } else if (strncmp(p, "fout", 4) == 0 && (isdigit((unsigned char)p[4]) || p[4] == '.')) {
            spec->fout_sec = atof(p + 4);
            spec->has_fout = 1;
            while (*p && !isspace((unsigned char)*p)) p++;
        } else if (strncmp(p, "in", 2) == 0 && (isdigit((unsigned char)p[2]) || p[2] == '.')) {
            spec->in_sec = atof(p + 2);
            spec->has_in = 1;
            while (*p && !isspace((unsigned char)*p)) p++;
        } else if (strncmp(p, "out", 3) == 0 && (isdigit((unsigned char)p[3]) || p[3] == '.')) {
            spec->out_sec = atof(p + 3);
            spec->has_out = 1;
            while (*p && !isspace((unsigned char)*p)) p++;
        } else if (strncmp(p, "at", 2) == 0 && (isdigit((unsigned char)p[2]) || p[2] == '.')) {
            spec->at_sec = atof(p + 2);
            spec->has_at = 1;
            while (*p && !isspace((unsigned char)*p)) p++;
        } else if (p[0] == 'v' && (isdigit((unsigned char)p[1]) || p[1] == '-' || p[1] == '.')) {
            spec->vol = powf(10.0f, atof(p + 1) / 20.0f);
            while (*p && !isspace((unsigned char)*p)) p++;
        } else {
            strncpy(spec->filepath, p, sizeof(spec->filepath));
            spec->filepath[sizeof(spec->filepath) - 1] = '\0';
            size_t len = strlen(spec->filepath);
            while (len > 0 && (spec->filepath[len-1] == '\n' || spec->filepath[len-1] == '\r' || isspace((unsigned char)spec->filepath[len-1]))) {
                spec->filepath[--len] = '\0';
            }
            break;
        }
    }
}

/* load and slice audio on-the-fly via ffmpeg pipe, optionally time-stretched
 * and pitch-shifted (key-lock). pitch is a frequency factor: 2^(st/12). */
int16_t *load_audio_slice(TrackSpec *spec, uint32_t *out_num_samples,
                                 float tempo, float pitch)
{
    char cmd[2048];
    char filt[512];
    char tempo_filt[128];
    if (tempo <= 0.0f) tempo = 1.0f;
    if (pitch <= 0.0f) pitch = 1.0f;
    tempo_filter(tempo_filt, sizeof(tempo_filt), tempo / pitch);
    if (pitch != 1.0f) {
        snprintf(filt, sizeof(filt), "aresample=48000,asetrate=48000*%.6g,aresample=48000,%s",
                 pitch, tempo_filt);
    } else {
        snprintf(filt, sizeof(filt), "%s", tempo_filt);
    }

    if (spec->has_in && spec->has_out) {
        float duration = spec->out_sec - spec->in_sec;
        if (duration < 0) duration = 0.1f;
        snprintf(cmd, sizeof(cmd), "ffmpeg -v quiet -ss %.3f -t %.3f -i \"%s\" -map 0:a:0 -af \"%s\" -f s16le -acodec pcm_s16le -ar %d -ac %d -",
                 spec->in_sec, duration, spec->filepath, filt, TARGET_SAMPLE_RATE, TARGET_CHANNELS);
    } else if (spec->has_in) {
        snprintf(cmd, sizeof(cmd), "ffmpeg -v quiet -ss %.3f -i \"%s\" -map 0:a:0 -af \"%s\" -f s16le -acodec pcm_s16le -ar %d -ac %d -",
                 spec->in_sec, spec->filepath, filt, TARGET_SAMPLE_RATE, TARGET_CHANNELS);
    } else {
        snprintf(cmd, sizeof(cmd), "ffmpeg -v quiet -i \"%s\" -map 0:a:0 -af \"%s\" -f s16le -acodec pcm_s16le -ar %d -ac %d -",
                 spec->filepath, filt, TARGET_SAMPLE_RATE, TARGET_CHANNELS);
    }

    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        fprintf(stderr, "Error opening pipe for %s\n", spec->filepath);
        return NULL;
    }

    size_t capacity = 4096 * 1024;
    size_t size = 0;
    int16_t *buffer = malloc(capacity);
    if (!buffer) {
        pclose(pipe);
        return NULL;
    }

    while (1) {
        if (size + 4096 > capacity) {
            capacity *= 2;
            int16_t *new_buf = realloc(buffer, capacity);
            if (!new_buf) {
                free(buffer);
                pclose(pipe);
                return NULL;
            }
            buffer = new_buf;
        }
        size_t bytes_read = fread((uint8_t *)buffer + size, 1, 4096, pipe);
        if (bytes_read == 0) break;
        size += bytes_read;
    }

    pclose(pipe);
    *out_num_samples = size / sizeof(int16_t);
    return buffer;
}

/* parse a dynamics arc: "t1:g1,t2:g2,..." (seconds -> linear gain) */
int render_parse_arc_str(const char *s, ArcPoint *arc, int maxn)
{
    int n = 0;
    char buf[1024];
    strncpy(buf, s, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    char *tok = strtok(buf, ",");
    while (tok && n < maxn) {
        char *colon = strchr(tok, ':');
        if (colon) {
            *colon = '\0';
            arc[n].t = atof(tok);
            arc[n].g = atof(colon + 1);
            if (arc[n].t >= 0) n++;
        }
        tok = strtok(NULL, ",");
    }
    return n;
}

static float track_raw_duration(const TrackSpec *t)
{
    if (t->has_in && t->has_out) {
        float d = t->out_sec - t->in_sec;
        return d < 0 ? 0.1f : d;
    }
    double sd = source_duration_sec(t->filepath);
    if (sd <= 0) return -1.0f;
    float d = (float)sd - (t->has_in ? t->in_sec : 0.0f);
    if (t->has_out && t->out_sec < d) d = t->out_sec;
    return d;
}

/* print mixing-diagnostic warnings for overlapping tracks (non-fatal) */
static void warn_clashes(const TrackSpec *tracks, int n,
                         const float *bpm, const char (*key)[32],
                         const Texture *tex, const float *dur, float target,
                         int bpm_mode, int keylock)
{
    for (int i = 0; i < n; i++) {
        float tempo_i = (bpm_mode && target > 0 && bpm[i] > 0) ? target / bpm[i] : 1.0f;
        float di = dur[i] < 0 ? -1.0f : dur[i] / tempo_i;
        if (di < 0) continue;
        for (int j = i + 1; j < n; j++) {
            float tempo_j = (bpm_mode && target > 0 && bpm[j] > 0) ? target / bpm[j] : 1.0f;
            float dj = dur[j] < 0 ? -1.0f : dur[j] / tempo_j;
            float ai = tracks[i].has_at ? tracks[i].at_sec : 0.0f;
            float aj = tracks[j].has_at ? tracks[j].at_sec : 0.0f;
            if (!(ai < aj + dj && aj < ai + di)) continue;
            float toverlap = ai > aj ? ai : aj;
            if (!keylock && key[i][0] && key[j][0] && key[i][0] != '?' && key[j][0] != '?') {
                int d = camelot_dist(key[i], key[j]);
                if (d > 1)
                    fprintf(stderr, "warning: tracks %d & %d overlap at t=%.1fs; keys %s vs %s (camelot dist %d)\n",
                            i + 1, j + 1, toverlap, key[i], key[j], d);
            }
            if (!bpm_mode && bpm[i] > 0 && bpm[j] > 0) {
                float r = bpm[i] > bpm[j] ? bpm[i] / bpm[j] : bpm[j] / bpm[i];
                if (r > 1.15f)
                    fprintf(stderr, "warning: tracks %d & %d overlap at t=%.1fs; tempos %.0f vs %.0f (x%.2f) - off-grid risk, try --bpm --snap\n",
                            i + 1, j + 1, toverlap, bpm[i], bpm[j], r);
            }
            if (tex[i].rhythm > 0.55f && tex[j].rhythm > 0.55f)
                fprintf(stderr, "warning: tracks %d & %d overlap at t=%.1fs; both pulsed (rhythmicity %.2f & %.2f) - consider v-6db on one\n",
                        i + 1, j + 1, toverlap, tex[i].rhythm, tex[j].rhythm);
        }
    }
    /* auto-bed: rhythmic layers with no ambient bed beneath them */
    for (int i = 0; i < n; i++) {
        float ti = tex[i].rhythm;
        if (ti < 0.30f) continue;
        float tempo_i = (bpm_mode && target > 0 && bpm[i] > 0) ? target / bpm[i] : 1.0f;
        float di = dur[i] < 0 ? -1.0f : dur[i] / tempo_i;
        if (di < 0) continue;
        float ai = tracks[i].has_at ? tracks[i].at_sec : 0.0f;
        int has_bed = 0;
        for (int j = 0; j < n && !has_bed; j++) {
            if (tex[j].rhythm >= 0.0f && tex[j].rhythm < 0.30f) {
                float tempo_j = (bpm_mode && target > 0 && bpm[j] > 0) ? target / bpm[j] : 1.0f;
                float dj = dur[j] < 0 ? -1.0f : dur[j] / tempo_j;
                if (dj < 0) continue;
                float aj = tracks[j].has_at ? tracks[j].at_sec : 0.0f;
                if (ai < aj + dj && aj < ai + di) has_bed = 1;
            }
        }
        if (!has_bed)
            fprintf(stderr, "warning: track %d (%s, rhythmicity %.2f) has no ambient bed under it at t=%.1fs - layer an ambient bed beneath for a build-up\n",
                    i + 1, texture_label(ti), ti, ai);
    }
}

/* next available take number in cwd (counts legacy tj_take_* too) */
static int next_take(void)
{
    glob_t g;
    int max = 0;
    static const char *patterns[] = { "gram_take_*.wav", "tj_take_*.wav" };
    for (size_t pi = 0; pi < sizeof(patterns) / sizeof(patterns[0]); pi++) {
        if (glob(patterns[pi], 0, NULL, &g) == 0) {
            for (size_t i = 0; i < g.gl_pathc; i++) {
                int n;
                const char *base = strncmp(patterns[pi], "gram", 4) == 0
                    ? "gram_take_%d" : "tj_take_%d";
                char dummy[64];
                (void)dummy;
                if (sscanf(g.gl_pathv[i], base, &n) == 1 && n > max) max = n;
            }
            globfree(&g);
        }
    }
    return max + 1;
}

void render_opts_defaults(RenderOpts *o)
{
    memset(o, 0, sizeof(*o));
    o->fade_in_default = FADE_IN_SECONDS;
    o->fade_out_default = FADE_OUT_SECONDS;
}

int render_opts_parse(int argc, char **argv, int start, RenderOpts *o)
{
    for (int i = start; i < argc; i++) {
        if (strcmp(argv[i], "--bpm") == 0) {
            o->bpm_mode = 1;
            if (i + 1 < argc && argv[i + 1][0] && isdigit((unsigned char)argv[i + 1][0])) {
                o->fixed_bpm = atof(argv[++i]);
                o->fixed = o->fixed_bpm > 0;
            }
        } else if (strncmp(argv[i], "--bpm=", 6) == 0) {
            o->bpm_mode = 1;
            o->fixed_bpm = atof(argv[i] + 6);
            o->fixed = o->fixed_bpm > 0;
        } else if (strcmp(argv[i], "--snap") == 0) {
            o->snap = 1;
        } else if (strcmp(argv[i], "--keylock") == 0) {
            o->keylock = 1;
            if (i + 1 < argc && argv[i + 1][0]
                && (isalpha((unsigned char)argv[i + 1][0]) || isdigit((unsigned char)argv[i + 1][0]))) {
                strncpy(o->keylock_target, argv[++i], sizeof(o->keylock_target) - 1);
            }
        } else if (strncmp(argv[i], "--keylock=", 10) == 0) {
            o->keylock = 1;
            strncpy(o->keylock_target, argv[i] + 10, sizeof(o->keylock_target) - 1);
        } else if (strcmp(argv[i], "--fade-in") == 0 && i + 1 < argc) {
            o->fade_in_default = atof(argv[++i]);
        } else if (strncmp(argv[i], "--fade-in=", 10) == 0) {
            o->fade_in_default = atof(argv[i] + 10);
        } else if (strcmp(argv[i], "--fade-out") == 0 && i + 1 < argc) {
            o->fade_out_default = atof(argv[++i]);
        } else if (strncmp(argv[i], "--fade-out=", 11) == 0) {
            o->fade_out_default = atof(argv[i] + 11);
        } else if (strcmp(argv[i], "--arc") == 0 && i + 1 < argc) {
            o->narc = render_parse_arc_str(argv[++i], o->arc, MAX_ARC);
        } else if (strncmp(argv[i], "--arc=", 6) == 0) {
            o->narc = render_parse_arc_str(argv[i] + 6, o->arc, MAX_ARC);
        } else if (strcmp(argv[i], "--master") == 0) {
            o->master_mode = 1;
            snprintf(o->master_graph, sizeof(o->master_graph),
                     "acompressor=threshold=-18dB:ratio=3:attack=20:release=250:makeup=1,"
                     "stereotools=base=0.2,alimiter=limit=0.7071");
            if (i + 1 < argc && argv[i + 1][0]) {
                if (strcmp(argv[i + 1], "pop") == 0) {
                    i++;
                } else if (strcmp(argv[i + 1], "subtle") == 0) {
                    i++;
                    snprintf(o->master_graph, sizeof(o->master_graph),
                             "acompressor=threshold=-12dB:ratio=2:attack=30:release=300,alimiter=limit=0.7071");
                } else if (strchr(argv[i + 1], '=') || strchr(argv[i + 1], ',')) {
                    snprintf(o->master_graph, sizeof(o->master_graph), "%s", argv[++i]);
                }
            }
        } else if (strncmp(argv[i], "--master=", 9) == 0) {
            o->master_mode = 1;
            const char *arg = argv[i] + 9;
            if (strcmp(arg, "pop") == 0) {
                snprintf(o->master_graph, sizeof(o->master_graph),
                         "acompressor=threshold=-18dB:ratio=3:attack=20:release=250:makeup=1,"
                         "stereotools=base=0.2,alimiter=limit=0.7071");
            } else if (strcmp(arg, "subtle") == 0) {
                snprintf(o->master_graph, sizeof(o->master_graph),
                         "acompressor=threshold=-12dB:ratio=2:attack=30:release=300,alimiter=limit=0.7071");
            } else {
                snprintf(o->master_graph, sizeof(o->master_graph), "%s", arg);
            }
        } else {
            fprintf(stderr, "gram render: unknown option '%s'\n", argv[i]);
            return -1;
        }
    }
    return 0;
}

int render_edl(const char *edl_src, const char *out_file, const RenderOpts *opts)
{
    RenderOpts defaults;
    if (!opts) {
        render_opts_defaults(&defaults);
        opts = &defaults;
    }

    char edl_str[2048];
    strncpy(edl_str, edl_src, sizeof(edl_str));
    edl_str[sizeof(edl_str) - 1] = '\0';

    int auto_name = (out_file == NULL);

    int bpm_mode = opts->bpm_mode;
    int fixed = opts->fixed;
    float fixed_bpm = opts->fixed_bpm;
    int snap = opts->snap;
    int keylock = opts->keylock;
    const char *keylock_target = opts->keylock_target;
    float fade_in_default = opts->fade_in_default;
    float fade_out_default = opts->fade_out_default;
    const ArcPoint *arc = opts->arc;
    int narc = opts->narc;
    int master_mode = opts->master_mode;
    const char *master_graph = opts->master_graph;

    TrackSpec tracks[MAX_TRACKS];
    int track_count = 0;

    char *token = strtok(edl_str, ",");
    while (token != NULL && track_count < MAX_TRACKS) {
        parse_track_spec(token, &tracks[track_count]);
        track_count++;
        token = strtok(NULL, ",");
    }

    if (track_count == 0) {
        fprintf(stderr, "No valid tracks specified in EDL string.\n");
        return 1;
    }

    float track_bpm[MAX_TRACKS] = {0};
    char track_key[MAX_TRACKS][32] = {{0}};
    Texture track_tex[MAX_TRACKS];
    for (int i = 0; i < MAX_TRACKS; i++) {
        memset(&track_tex[i], 0, sizeof(Texture));
        track_tex[i].rhythm = -1.0f;
    }
    float target = 0.0f;

    float track_dur[MAX_TRACKS];
    for (int i = 0; i < track_count; i++) {
        analyze_track(tracks[i].filepath, &track_bpm[i], track_key[i], sizeof(track_key[i]), &track_tex[i]);
        track_dur[i] = track_raw_duration(&tracks[i]);
    }

    if (bpm_mode) {
        float valid[MAX_TRACKS];
        int nvalid = 0;
        for (int i = 0; i < track_count; i++) {
            if (track_bpm[i] > 0 && nvalid < MAX_TRACKS) valid[nvalid++] = track_bpm[i];
        }
        if (fixed) {
            target = fixed_bpm;
        } else if (nvalid > 0) {
            target = median_bpm(valid, nvalid);
        }
        if (target > 0) {
            printf("Target BPM: %.1f (%s)\n", target, fixed ? "fixed" : "auto median");
            if (snap) printf("Snapping in/out/at times to the beat grid.\n");
        } else {
            fprintf(stderr, "Warning: no BPM detected; mixing without tempo matching.\n");
        }
    }

    /* key-lock: transpose every track to a shared key (opt-in) */
    float pitch[MAX_TRACKS];
    for (int i = 0; i < track_count; i++) pitch[i] = 1.0f;
    char target_key[16] = "";
    if (keylock) {
        if (keylock_target[0] && strcasecmp(keylock_target, "auto") != 0) {
            snprintf(target_key, sizeof(target_key), "%s", keylock_target);
        } else {
            int best = -1, bestc = 0;
            for (int i = 0; i < track_count; i++) {
                if (!track_key[i][0] || track_key[i][0] == '?') continue;
                int c = 0;
                for (int j = 0; j < track_count; j++)
                    if (track_key[j][0] && strcmp(track_key[i], track_key[j]) == 0) c++;
                if (c > bestc) { bestc = c; best = i; }
            }
            if (best >= 0) strncpy(target_key, track_key[best], sizeof(target_key) - 1);
        }
        if (!target_key[0]) {
            fprintf(stderr, "Warning: --keylock requested but no usable keys detected.\n");
        } else {
            int tp = camelot_pitch(target_key);
            if (tp >= 0) {
                printf("Key-lock target: %s\n", target_key);
                for (int i = 0; i < track_count; i++) {
                    int sp = camelot_pitch(track_key[i]);
                    if (sp < 0) continue;
                    if (sp != tp) {
                        int delta = ((tp - sp) % 12 + 12) % 12;
                        if (delta > 6) delta -= 12;
                        pitch[i] = powf(2.0f, (float)delta / 12.0f);
                        printf("  transpose %s: %s -> %s (%+d st)\n", tracks[i].filepath,
                               track_key[i], target_key, delta);
                    }
                }
            }
        }
    }

    warn_clashes(tracks, track_count, track_bpm, track_key, track_tex, track_dur, target, bpm_mode, keylock);

    int16_t *track_buffers[MAX_TRACKS];
    uint32_t track_samples[MAX_TRACKS];
    uint32_t max_total_samples = 0;

    for (int i = 0; i < track_count; i++) {
        float tempo = 1.0f;
        if (bpm_mode && target > 0 && track_bpm[i] > 0) {
            tempo = target / track_bpm[i];
            if (snap) {
                float sbpm = track_bpm[i];
                if (tracks[i].has_in) tracks[i].in_sec = snap_beat(tracks[i].in_sec, sbpm);
                if (tracks[i].has_out) tracks[i].out_sec = snap_beat(tracks[i].out_sec, sbpm);
                if (tracks[i].has_at) tracks[i].at_sec = snap_beat(tracks[i].at_sec, target);
            }
        }
        if (bpm_mode && track_bpm[i] > 0) {
            if (tempo != 1.0f)
                printf("Processing [%d/%d]: %s (%.1f BPM, %s, tempo x%.3f)\n", i + 1,
                       track_count, tracks[i].filepath, track_bpm[i], track_key[i], tempo);
            else
                printf("Processing [%d/%d]: %s (%.1f BPM, %s)\n", i + 1,
                       track_count, tracks[i].filepath, track_bpm[i], track_key[i]);
        } else if (bpm_mode && track_key[i][0] && track_key[i][0] != '?') {
            printf("Processing [%d/%d]: %s (no beat grid, %s)\n", i + 1,
                   track_count, tracks[i].filepath, track_key[i]);
        } else {
            printf("Processing [%d/%d]: %s\n", i + 1, track_count, tracks[i].filepath);
        }
        if (pitch[i] != 1.0f)
            printf("  pitch x%.4f (key-lock)\n", pitch[i]);
        track_buffers[i] = load_audio_slice(&tracks[i], &track_samples[i], tempo, pitch[i]);
        if (!track_buffers[i]) {
            fprintf(stderr, "Failed to load/slice track: %s\n", tracks[i].filepath);
            for (int j = 0; j < i; j++) free(track_buffers[j]);
            return 1;
        }

        uint32_t offset_samples = tracks[i].has_at ? (uint32_t)(tracks[i].at_sec * TARGET_SAMPLE_RATE * TARGET_CHANNELS) : 0;
        uint32_t total_end = offset_samples + track_samples[i];
        if (total_end > max_total_samples) {
            max_total_samples = total_end;
        }
    }

    float *out_buf = calloc(max_total_samples, sizeof(float));
    if (!out_buf) {
        fprintf(stderr, "Memory allocation failed for output timeline.\n");
        for (int i = 0; i < track_count; i++) free(track_buffers[i]);
        return 1;
    }

    uint32_t fade_in_samples = (uint32_t)(TARGET_SAMPLE_RATE * TARGET_CHANNELS * fade_in_default);
    uint32_t fade_out_samples = (uint32_t)(TARGET_SAMPLE_RATE * TARGET_CHANNELS * fade_out_default);

    for (int i = 0; i < track_count; i++) {
        uint32_t offset_samples = tracks[i].has_at ? (uint32_t)(tracks[i].at_sec * TARGET_SAMPLE_RATE * TARGET_CHANNELS) : 0;
        uint32_t num_samps = track_samples[i];
        int16_t *src = track_buffers[i];
        float vol = tracks[i].vol;

        uint32_t fin = tracks[i].has_fin
            ? (uint32_t)(tracks[i].fin_sec * TARGET_SAMPLE_RATE * TARGET_CHANNELS)
            : fade_in_samples;
        uint32_t fout = tracks[i].has_fout
            ? (uint32_t)(tracks[i].fout_sec * TARGET_SAMPLE_RATE * TARGET_CHANNELS)
            : fade_out_samples;
        if (fin > num_samps / 2) fin = num_samps / 2;
        if (fout > num_samps / 2) fout = num_samps / 2;

        for (uint32_t j = 0; j < num_samps; j++) {
            uint32_t timeline_idx = offset_samples + j;
            if (timeline_idx >= max_total_samples) break;

            float gain = 1.0f;
            if (fin > 0 && j < fin) {
                /* equal-power sine-law fade-in */
                float progress = (float)j / (float)fin;
                gain = sinf(progress * (M_PI / 2.0f));
            } else if (fout > 0 && j > num_samps - fout) {
                /* equal-power sine-law fade-out */
                float progress = (float)(num_samps - j) / (float)fout;
                gain = sinf(progress * (M_PI / 2.0f));
            }

            out_buf[timeline_idx] += (float)src[j] * gain * vol;
        }
    }

    /* master dynamics arc: time -> gain automation over the whole timeline */
    if (narc > 0) {
        int seg = 0;
        for (uint32_t i = 0; i + 1 < max_total_samples; i += TARGET_CHANNELS) {
            float t = (float)(i / TARGET_CHANNELS) / TARGET_SAMPLE_RATE;
            while (seg + 1 < narc && t > arc[seg + 1].t) seg++;
            float g;
            if (t <= arc[0].t) g = arc[0].g;
            else if (seg + 1 >= narc) g = arc[narc - 1].g;
            else {
                float span = arc[seg + 1].t - arc[seg].t;
                float f = span > 0 ? (t - arc[seg].t) / span : 0.0f;
                g = arc[seg].g + f * (arc[seg + 1].g - arc[seg].g);
            }
            out_buf[i] *= g;
            out_buf[i + 1] *= g;
        }
    }

    /* fixed -3 dB headroom */
    const float scale = 0.7071f;

    char auto_out[64];
    if (auto_name) {
        int mins = (int)lroundf((float)max_total_samples / (TARGET_SAMPLE_RATE * TARGET_CHANNELS) / 60.0f);
        snprintf(auto_out, sizeof(auto_out), "gram_take_%03d_%dmins_mix.wav", next_take(), mins);
        out_file = auto_out;
    }

    int status = 0;
    if (write_wav(out_file, out_buf, max_total_samples, scale)) {
        char edl_path[1024];
        snprintf(edl_path, sizeof edl_path, "%s", out_file);
        size_t len = strlen(edl_path);
        if (len > 4 && strcmp(edl_path + len - 4, ".wav") == 0)
            snprintf(edl_path + len - 4, sizeof edl_path - (len - 4), ".edl");
        else
            snprintf(edl_path + len, sizeof edl_path - len, ".edl");
        FILE *fp = fopen(edl_path, "w");
        if (fp) {
            fprintf(fp, "%s\n", edl_src);
            fclose(fp);
        }
        printf("Successfully rendered EDL -> %s (Length: %.2f seconds, EDL: %s)\n",
               out_file, (float)max_total_samples / (TARGET_SAMPLE_RATE * TARGET_CHANNELS), edl_path);
    } else {
        fprintf(stderr, "Failed to write output WAV file.\n");
        status = 1;
    }

    if (!status && master_mode && master_graph[0]) {
        char master_out[1024];
        snprintf(master_out, sizeof(master_out), "%s", out_file);
        size_t l = strlen(master_out);
        if (l > 4 && strcmp(master_out + l - 4, ".wav") == 0)
            snprintf(master_out + l - 4, sizeof(master_out) - (l - 4), "_master.wav");
        else
            snprintf(master_out + l, sizeof(master_out) - l, "_master.wav");
        char cmd[8192];
        snprintf(cmd, sizeof(cmd), "ffmpeg -v warning -y -i \"%s\" -af \"%s\" -c:a pcm_s16le \"%s\"",
                 out_file, master_graph, master_out);
        if (system(cmd) == 0) {
            printf("Mastered -> %s\n", master_out);
            brickwall_limit(master_out, 0.7071f);
        } else
            fprintf(stderr, "Mastering failed (check the filter graph): %s\n", master_graph);
    }

    for (int i = 0; i < track_count; i++)
        free(track_buffers[i]);
    free(out_buf);
    return status;
}
