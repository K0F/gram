#include "analysis.h"

#include "util.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void cache_dir(char *buf, size_t n)
{
    const char *home = getenv("HOME");
    if (home) snprintf(buf, n, "%s/.cache/tj", home);
    else snprintf(buf, n, ".tj_cache");
}

static void cache_path(const char *file, const char *ext, char *buf, size_t n)
{
    char dir[512];
    cache_dir(dir, sizeof(dir));
    mkdir(dir, 0700);
    snprintf(buf, n, "%s/%08lx.%s", dir, fnv1a(file), ext);
}

static int read_cached_float(const char *file, const char *ext, float *val)
{
    char path[1024];
    cache_path(file, ext, path, sizeof(path));
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    if (fscanf(fp, "%f", val) != 1) { fclose(fp); return 0; }
    fclose(fp);
    return 1;
}

static void write_cached_float(const char *file, const char *ext, float val)
{
    char path[1024];
    cache_path(file, ext, path, sizeof(path));
    FILE *fp = fopen(path, "w");
    if (!fp) return;
    fprintf(fp, "%.4f\n", val);
    fclose(fp);
}

static int read_cached_str(const char *file, const char *ext, char *val, size_t n)
{
    char path[1024];
    cache_path(file, ext, path, sizeof(path));
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    if (!fgets(val, (int)n, fp)) { fclose(fp); return 0; }
    fclose(fp);
    trim_ws(val);
    return val[0] != '\0';
}

static void write_cached_str(const char *file, const char *ext, const char *val)
{
    char path[1024];
    cache_path(file, ext, path, sizeof(path));
    FILE *fp = fopen(path, "w");
    if (!fp) return;
    fprintf(fp, "%s\n", val);
    fclose(fp);
}

const char *texture_label(float rhythm)
{
    if (rhythm < 0) return "n/a";
    if (rhythm < 0.30f) return "ambient";
    if (rhythm < 0.55f) return "motion";
    return "pulse";
}

float texture_score(const Texture *t)
{
    float density_n = clampf(log1pf(t->density) / log1pf(8.0f), 0.0f, 1.0f);
    float steady_n  = clampf(t->steady / 2.0f, 0.0f, 1.0f);
    return 0.55f * density_n + 0.30f * clampf(t->pulse, 0.0f, 1.0f) + 0.15f * steady_n;
}

static int read_cached_tex(const char *file, Texture *t)
{
    char path[1024];
    cache_path(file, "tex", path, sizeof(path));
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    char ver[8];
    int ok = fscanf(fp, "%7s %f %f %f %f", ver, &t->density, &t->pulse,
                    &t->steady, &t->zcr) == 5
        && strcmp(ver, "tex2") == 0;
    fclose(fp);
    if (ok) t->rhythm = texture_score(t);
    return ok;
}

static void write_cached_tex(const char *file, const Texture *t)
{
    char path[1024];
    cache_path(file, "tex", path, sizeof(path));
    FILE *fp = fopen(path, "w");
    if (!fp) return;
    fprintf(fp, "tex2 %.4f %.4f %.4f %.4f\n", t->density, t->pulse, t->steady, t->zcr);
    fclose(fp);
}

/* Analysis windows are capped so long recordings analyze quickly. */
#define ANALYSIS_WINDOW 60

static int decode_analysis_window(const char *file, char *wav, size_t n, double start_sec)
{
    snprintf(wav, n, "/tmp/gram_analyze_%d.wav", (int)getpid());
    char cmd[4096];
    if (start_sec > 0)
        snprintf(cmd, sizeof(cmd), "ffmpeg -v quiet -y -ss %.3f -i \"%s\" -map 0:a:0 -ac 1 -ar 44100 -t %d -f wav \"%s\"",
                 start_sec, file, ANALYSIS_WINDOW, wav);
    else
        snprintf(cmd, sizeof(cmd), "ffmpeg -v quiet -y -i \"%s\" -map 0:a:0 -ac 1 -ar 44100 -t %d -f wav \"%s\"",
                 file, ANALYSIS_WINDOW, wav);
    return system(cmd) == 0 ? 0 : -1;
}

double source_duration_sec(const char *file)
{
    char cmd[4096];
    char out[256] = "";
    snprintf(cmd, sizeof(cmd), "ffprobe -v error -show_entries format=duration -of default=nk=1:nw=1 \"%s\" 2>/dev/null", file);
    if (run_capture(cmd, out, sizeof(out)) != 0) return -1.0;
    double d = atof(out);
    return d > 0 ? d : -1.0;
}

/* BPM via soundstretch (SoundTouch) */
static float detect_bpm(char *wav)
{
    char cmd[4096];
    char out[1024] = "";
    snprintf(cmd, sizeof(cmd), "soundstretch \"%s\" -bpm 2>&1", wav);
    if (run_capture(cmd, out, sizeof(out)) != 0) return 0.0f;
    const char *p = strstr(out, "Detected BPM rate");
    if (!p) return 0.0f;
    float bpm = atof(p + strlen("Detected BPM rate"));
    if (bpm < 30.0f || bpm > 500.0f) return 0.0f;
    return bpm;
}

/* musical key via keyfinder-cli (libKeyFinder) */
static void detect_key(char *wav, char *key, size_t keylen)
{
    char cmd[4096];
    char out[256] = "";
    snprintf(cmd, sizeof(cmd), "keyfinder-cli -n camelot \"%s\" 2>/dev/null", wav);
    if (run_capture(cmd, out, sizeof(out)) == 0) {
        trim_ws(out);
        snprintf(key, keylen, "%s", out[0] ? out : "?");
    } else {
        snprintf(key, keylen, "?");
    }
}

int camelot_pitch(const char *key)
{
    if (!key || !key[0] || key[0] == '?') return -1;
    int n = atoi(key);
    if (n < 1 || n > 12) return -1;
    int major = strchr(key, 'B') != NULL;
    if (!major && !strchr(key, 'A')) return -1;
    int p = major ? (n - 1) * 7 : 11 - (n - 1) * 5;
    return ((p % 12) + 12) % 12;
}

int camelot_dist(const char *a, const char *b)
{
    int pa = camelot_pitch(a), pb = camelot_pitch(b);
    if (pa < 0 || pb < 0) return -1;
    int d = ((pa - pb) % 12 + 12) % 12;
    if (d > 6) d = 12 - d;
    return d;
}

/* Analyze rhythmic texture from a decoded mono 44.1k int16 WAV:
 * onset density (aubioonset) + a single streaming pass for envelope stats. */
static int detect_texture(const char *wav, Texture *tex)
{
    memset(tex, 0, sizeof(*tex));
    tex->rhythm = -1.0f;

    char cmd[4096];
    static char out[262144];
    snprintf(cmd, sizeof(cmd), "aubioonset \"%s\" 2>/dev/null", wav);
    int have_onsets = run_capture(cmd, out, sizeof(out)) == 0 && out[0] != '\0';

    FILE *fp = fopen(wav, "rb");
    if (!fp) return 0;

    uint32_t rate = 0;
    uint16_t block_align = 0;
    long data_pos = -1;
    uint32_t data_size = 0;
    if (wav_data_info(fp, &rate, &block_align, &data_pos, &data_size) != 0) {
        fclose(fp);
        return 0;
    }
    if (rate == 0 || block_align == 0) { fclose(fp); return 0; }

    if (have_onsets) {
        int n = 0;
        char *p = out;
        while (*p) {
            while (*p && isspace((unsigned char)*p)) p++;
            if (!*p) break;
            n++;
            while (*p && !isspace((unsigned char)*p)) p++;
        }
        float dur = (float)data_size / (float)block_align / (float)rate;
        if (dur > 0) tex->density = (float)n / dur;
    }

    if (fseek(fp, data_pos, SEEK_SET) != 0) { fclose(fp); return 0; }
    int16_t *s = malloc(data_size);
    if (!s) { fclose(fp); return 0; }
    size_t got = fread(s, 1, data_size, fp);
    fclose(fp);
    size_t nsamp = got / sizeof(int16_t);
    if (nsamp < 1024) { free(s); return 0; }

    const int FRAME = 1024, HOP = 512;
    int nframes = (int)((nsamp - FRAME) / HOP) + 1;
    float *E = malloc((size_t)nframes * sizeof(float));
    float *Z = malloc((size_t)nframes * sizeof(float));
    float *S = malloc((size_t)nframes * sizeof(float));
    if (!E || !Z || !S) {
        free(E); free(Z); free(S); free(s);
        return 0;
    }

    double esum = 0.0;
    for (int i = 0; i < nframes; i++) {
        size_t off = (size_t)i * HOP;
        double e = 0.0;
        long zc = 0;
        int prev = 0;
        for (int j = 0; j < FRAME; j++) {
            int v = s[off + j];
            e += (double)v * v;
            if (j > 0 && ((prev < 0) != (v < 0))) zc++;
            prev = v;
        }
        float rms = (float)sqrt(e / FRAME);
        E[i] = rms;
        Z[i] = (float)zc / (float)FRAME;
        esum += E[i];
    }
    free(s);

    double mean = esum / nframes;
    double var = 0.0;
    for (int i = 0; i < nframes; i++) var += ((double)E[i] - mean) * ((double)E[i] - mean);
    var /= nframes;
    tex->steady = mean > 0 ? (float)(sqrt(var) / mean) : 0.0f;

    double zsum = 0.0;
    for (int i = 0; i < nframes; i++) zsum += Z[i];
    tex->zcr = (float)(zsum / nframes);
    free(Z);

    /* flux = rectified rise in frame energy */
    for (int i = 0; i < nframes; i++)
        S[i] = i > 0 ? fmaxf(0.0f, E[i] - E[i - 1]) : E[0];
    float smax = 0.0f;
    for (int i = 0; i < nframes; i++) if (S[i] > smax) smax = S[i];
    if (smax > 0) for (int i = 0; i < nframes; i++) S[i] /= smax;
    free(E);

    /* autocorrelate the flux envelope over periods of 0.2s..2.0s */
    double smean = 0.0;
    for (int i = 0; i < nframes; i++) smean += S[i];
    smean /= nframes;
    double svar = 0.0;
    for (int i = 0; i < nframes; i++) svar += ((double)S[i] - smean) * ((double)S[i] - smean);
    svar /= nframes;

    float dt = (float)HOP / (float)rate;
    int lag0 = (int)(0.2f / dt + 0.5f); if (lag0 < 1) lag0 = 1;
    int lag1 = (int)(2.0f / dt + 0.5f); if (lag1 >= nframes) lag1 = nframes - 1;
    float best = 0.0f;
    if (svar > 1e-12 && lag1 > lag0) {
        for (int lag = lag0; lag <= lag1; lag++) {
            double acc = 0.0;
            for (int i = 0; i < nframes - lag; i++)
                acc += ((double)S[i] - smean) * ((double)S[i + lag] - smean);
            double ac = acc / (svar * (double)(nframes - lag));
            if (ac > best) best = (float)ac;
        }
    }
    free(S);
    tex->pulse = best;
    tex->rhythm = texture_score(tex);
    return 1;
}

int analyze_track(const char *file, float *bpm, char *key, size_t keylen, Texture *tex)
{
    int have_bpm = read_cached_float(file, "bpm", bpm);
    int have_key = read_cached_str(file, "key", key, keylen);
    int have_tex = read_cached_tex(file, tex);
    if (have_bpm && have_key && have_tex) return 1;

    char wav[512];
    if (decode_analysis_window(file, wav, sizeof(wav), 0.0) != 0) return 0;

    if (!have_bpm) {
        float b = detect_bpm(wav);
        if (b > 0) {
            *bpm = b;
            write_cached_float(file, "bpm", b);
            have_bpm = 1;
        }
    }
    if (!have_key) {
        char k[32];
        detect_key(wav, k, sizeof(k));
        if (k[0] && k[0] != '?') {
            snprintf(key, keylen, "%s", k);
            write_cached_str(file, "key", k);
            have_key = 1;
        } else {
            snprintf(key, keylen, "?");
        }
    }
    if (!have_tex) {
        Texture t;
        if (detect_texture(wav, &t)) {
            /* long sources: also sample a middle window and keep the more
             * rhythmic one, so a quiet intro can't hide a pulsing body */
            double dur = source_duration_sec(file);
            if (dur > 2.0 * ANALYSIS_WINDOW) {
                double mid = (dur - ANALYSIS_WINDOW) / 2.0;
                if (decode_analysis_window(file, wav, sizeof(wav), mid) == 0) {
                    Texture tmid;
                    if (detect_texture(wav, &tmid) && tmid.density > t.density) t = tmid;
                }
            }
            *tex = t;
            write_cached_tex(file, &t);
            have_tex = 1;
        }
    }

    unlink(wav);
    return have_bpm || have_key || have_tex;
}
