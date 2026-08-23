#ifndef GRAM_AV_RENDER_H
#define GRAM_AV_RENDER_H

#include "library.h"
#include "render.h"

/*
 * AV compositor: renders the visual edit corresponding to an audio EDL
 * and muxes it with the rendered mix into an MP4 (H.264 + AAC).
 *
 * The .vedl sidecar maps each music EDL entry (by index) to a gesture:
 *   "<idx> <op> <gen>"   op in {+,-,x,/}  gen in {file,scope,wave}
 *
 * 'file' entries stream frames from the video pool at the same relative
 * timecode as the audio slice; procedural generators render from the
 * slice's own PCM. Frames are piped raw to ffmpeg/libx264.
 */

typedef struct {
    int w, h, fps;
} AvOpts;

void av_opts_defaults(AvOpts *o);

/* vid_dir may be NULL/empty (all entries fall back to procedural). */
int av_render(const char *music_edl, const char *vedl, const char *arc_str,
              const char *audio_wav, const char *vid_dir, const char *out_mp4,
              const AvOpts *o);

#endif
