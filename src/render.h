#ifndef GRAM_RENDER_H
#define GRAM_RENDER_H

#include <stddef.h>
#include <stdint.h>

/*
 * Offline EDL mixdown engine ported from tj. Renders an edit decision
 * list string into a 48 kHz stereo WAV: per-track slices, fades, volume,
 * optional tempo-match (--bpm), beat snap, Camelot key-lock, master gain
 * arc and an ffmpeg mastering chain.
 */

#define TARGET_SAMPLE_RATE 48000
#define TARGET_CHANNELS 2
#define MAX_TRACKS 256
#define MAX_ARC 64

typedef struct {
    char filepath[512];
    float in_sec;
    float out_sec;
    int has_in;
    int has_out;
    float at_sec;
    int has_at;
    float vol;
    float fin_sec;
    float fout_sec;
    int has_fin;
    int has_fout;
} TrackSpec;

typedef struct {
    float t, g;
} ArcPoint;

typedef struct {
    int bpm_mode;
    int fixed;
    float fixed_bpm;
    int snap;
    int keylock;
    char keylock_target[16];
    float fade_in_default;
    float fade_out_default;
    ArcPoint arc[MAX_ARC];
    int narc;
    int master_mode;
    char master_graph[4096];
} RenderOpts;

void render_opts_defaults(RenderOpts *o);

/* parse option tokens (argv[start..argc-1]) into o; accepts the same
 * spellings as the original tj CLI; returns 0 on success */
int render_opts_parse(int argc, char **argv, int start, RenderOpts *o);

void parse_track_spec(const char *str, TrackSpec *spec);

/* shared helpers used by the AV compositor */
int16_t *load_audio_slice(TrackSpec *spec, uint32_t *out_num_samples,
                          float tempo, float pitch);
int render_parse_arc_str(const char *s, ArcPoint *arc, int maxn);

/* master entry point. edl_src: comma-separated EDL.
 * out_file: output path, or NULL for auto take naming.
 * returns process exit status (0 = ok). */
int render_edl(const char *edl_src, const char *out_file, const RenderOpts *o);

/* hard brickwall clip to +-limit on a written WAV file */
void brickwall_limit(const char *path, float limit);

#endif
