#ifndef GRAM_ANALYSIS_H
#define GRAM_ANALYSIS_H

#include <stddef.h>
#include <stdint.h>

/*
 * Audio analysis ported from tj: BPM (soundstretch), Camelot key
 * (keyfinder-cli) and rhythmic texture (aubioonset + own DSP).
 * Results are cached in ~/.cache/tj — shared with the original tj tool.
 */

typedef struct {
    float density;   /* onsets per second (aubioonset) */
    float pulse;     /* 0..1 transient periodicity (envelope autocorrelation) */
    float steady;    /* coefficient of variation of frame RMS (low = steady bed) */
    float zcr;       /* mean zero-crossing rate (brightness / spectral tilt) */
    float rhythm;    /* composite 0..1 rhythmicity score, or -1 if unknown */
} Texture;

const char *texture_label(float rhythm);
float texture_score(const Texture *t);

/* total duration of a source in seconds via ffprobe, or -1 if unknown */
double source_duration_sec(const char *file);

/* analyze one track (BPM + key + texture), cached; returns nonzero if any
 * component succeeded */
int analyze_track(const char *file, float *bpm, char *key, size_t keylen,
                  Texture *tex);

/* Camelot key math: "8A" -> pitch class 0..11 (C=0), -1 if unparsable */
int camelot_pitch(const char *key);
int camelot_dist(const char *a, const char *b);

#endif
