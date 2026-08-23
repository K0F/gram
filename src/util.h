#ifndef GRAM_UTIL_H
#define GRAM_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

void die(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

double clampd(double x, double lo, double hi);
float clampf(float x, float lo, float hi);

/* deterministic xorshift64* RNG (shared by the rng planning engine) */
extern uint64_t rng_state;
uint64_t rnd_next(void);
double rnd_unit(void);
double rnd_range(double lo, double hi);
void shuffle_ints(int *a, int n);
void rng_seed(uint64_t seed);

int has_audio_ext(const char *name);
void sh_quote(char *dst, size_t n, const char *src);
const char *path_tail(const char *p);
void trim_ws(char *s);
unsigned long fnv1a(const char *s);

typedef struct {
    float t, v;
} EnvPt;

float env_eval(const EnvPt *e, int n, float t);

/* capture combined stdout+stderr of a shell command */
int run_capture(const char *cmd, char *out, size_t outlen);

/* walk a RIFF/WAVE file to its PCM data chunk (handles LIST/INFO chunks) */
int wav_data_info(FILE *fp, uint32_t *rate, uint16_t *block_align,
                  long *data_pos, uint32_t *data_size);

#endif
