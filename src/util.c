#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "gram: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) die("out of memory");
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) die("out of memory");
    return q;
}

char *xstrdup(const char *s)
{
    char *p = xmalloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}

double clampd(double x, double lo, double hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

uint64_t rng_state = 0x9E3779B97F4A7C15ULL;

void rng_seed(uint64_t seed)
{
    rng_state = seed;
    for (int i = 0; i < 8; i++) rnd_next();
}

uint64_t rnd_next(void)
{
    uint64_t x = rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng_state = x;
    return x * 2685821657736338717ULL;
}

double rnd_unit(void)
{
    return (double)(rnd_next() >> 11) * (1.0 / 9007199254740992.0);
}

double rnd_range(double lo, double hi)
{
    return lo + (hi - lo) * rnd_unit();
}

void shuffle_ints(int *a, int n)
{
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(rnd_next() % (uint64_t)(i + 1));
        int t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}

int has_audio_ext(const char *name)
{
    static const char *exts[] = { ".wav", ".flac", ".mp3", ".opus", ".ogg", ".m4a" };
    size_t len = strlen(name);
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        size_t el = strlen(exts[i]);
        if (len > el && strcasecmp(name + len - el, exts[i]) == 0) return 1;
    }
    return 0;
}

void sh_quote(char *dst, size_t n, const char *src)
{
    size_t w = 0;
    if (n < 4) { dst[0] = 0; return; }
    dst[w++] = '"';
    for (const char *p = src; *p && w + 2 < n; p++) {
        if (*p == '"' || *p == '\\' || *p == '$' || *p == '`') dst[w++] = '\\';
        dst[w++] = *p;
    }
    dst[w++] = '"';
    dst[w] = 0;
}

const char *path_tail(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

void trim_ws(char *s)
{
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

unsigned long fnv1a(const char *s)
{
    unsigned long h = 14695981039346656037UL;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 1099511628211UL;
    }
    return h;
}

float env_eval(const EnvPt *e, int n, float t)
{
    if (n == 0) return 0.0f;
    if (t <= e[0].t) return e[0].v;
    if (t >= e[n - 1].t) return e[n - 1].v;
    for (int i = 0; i + 1 < n; i++) {
        if (t >= e[i].t && t <= e[i + 1].t) {
            float span = e[i + 1].t - e[i].t;
            float f = span > 0.0f ? (t - e[i].t) / span : 0.0f;
            return e[i].v + f * (e[i + 1].v - e[i].v);
        }
    }
    return e[n - 1].v;
}

int run_capture(const char *cmd, char *out, size_t outlen)
{
    if (outlen == 0) return system(cmd) == 0 ? 0 : -1;
    FILE *p = popen(cmd, "r");
    if (!p) return -1;
    size_t total = 0;
    while (total + 1 < outlen) {
        size_t r = fread(out + total, 1, outlen - total - 1, p);
        if (r == 0) break;
        total += r;
    }
    out[total] = '\0';
    int rc = pclose(p);
    return rc == 0 ? 0 : -1;
}

int wav_data_info(FILE *fp, uint32_t *rate, uint16_t *block_align,
                  long *data_pos, uint32_t *data_size)
{
    char id[4];
    if (fread(id, 1, 4, fp) != 4 || memcmp(id, "RIFF", 4) != 0) return -1;
    if (fseek(fp, 4, SEEK_CUR) != 0) return -1;
    if (fread(id, 1, 4, fp) != 4 || memcmp(id, "WAVE", 4) != 0) return -1;
    for (;;) {
        uint32_t sz;
        if (fread(id, 1, 4, fp) != 4) break;
        if (fread(&sz, 4, 1, fp) != 1) break;
        long chunk_start = ftell(fp);
        if (memcmp(id, "fmt ", 4) == 0) {
            uint16_t fmt, ch;
            if (fread(&fmt, 2, 1, fp) != 1 || fread(&ch, 2, 1, fp) != 1) break;
            if (fread(rate, 4, 1, fp) != 1) break;
            uint32_t byterate;
            if (fread(&byterate, 4, 1, fp) != 1) break;
            if (fread(block_align, 2, 1, fp) != 1) break;
        } else if (memcmp(id, "data", 4) == 0) {
            *data_pos = chunk_start;
            *data_size = sz;
            return 0;
        }
        if (fseek(fp, chunk_start + (long)sz + (long)(sz & 1), SEEK_SET) != 0) break;
    }
    return -1;
}
