#include "title.h"

#include "analysis.h"
#include "gomfont.h"
#include "util.h"
#include "visual.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TITLE_TARGET_FRAC 0.84
#define TITLE_MIN_PX 24
#define TITLE_MAX_PX 160

int title_fit_size(int raster_w, float widest100)
{
    if (widest100 <= 0.0f) return TITLE_MAX_PX;
    double s = 100.0 * ((double)raster_w * TITLE_TARGET_FRAC) /
               (double)widest100 + 0.5;
    if (s < TITLE_MIN_PX) s = TITLE_MIN_PX;
    if (s > TITLE_MAX_PX) s = TITLE_MAX_PX;
    return (int)s;
}

static int decode_to_frame(const char *path, Frame *f)
{
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "ffmpeg -v quiet -i \"%s\" -f rawvideo -pix_fmt rgb24 "
             "-vf scale=%d:%d:force_original_aspect_ratio=increase,crop=%d:%d -",
             path, f->w, f->h, f->w, f->h);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    size_t want = (size_t)f->w * f->h * 3;
    size_t got = fread(f->px, 1, want, fp);
    pclose(fp);
    return got == want ? 0 : -1;
}

static void to_grayscale8(Frame *f)
{
    for (int p = 0; p < f->w * f->h; p++) {
        uint8_t y = (uint8_t)(0.299f * f->px[p * 3 + 0] +
                              0.587f * f->px[p * 3 + 1] +
                              0.114f * f->px[p * 3 + 2]);
        f->px[p * 3 + 0] = y;
        f->px[p * 3 + 1] = y;
        f->px[p * 3 + 2] = y;
    }
}

int title_run(const char *const *lines, int nlines, const char *stone,
              const char *out_mp4, int w, int h, int fps, double dur,
              int size_px, int mute)
{
    if (nlines <= 0) die("title: at least one -t line required");
    if (dur <= 0.0) dur = 2.0;
    if (w <= 0 || h <= 0 || fps <= 0) { w = 932; h = 576; fps = 25; }

    Frame *frame = frame_new(w, h);
    if (stone && stone[0] && decode_to_frame(stone, frame) == 0) {
        to_grayscale8(frame);
    } else {
        frame_clear(frame, 0, 0, 0);
    }

    int size = size_px;
    if (size <= 0) {
        Frame *probe = frame_new(w, h);
        float widest = 0.0f;
        for (int i = 0; i < nlines; i++) {
            float adv = gf_render(probe, lines[i], (float)w * 0.5f,
                                  (float)h * 0.5f, 100.0f, 255, 255, 255);
            if (adv > widest) widest = adv;
        }
        frame_free(probe);
        size = title_fit_size(w, widest);
        printf("title: auto-size %dpx (widest %.1fpx at cap 100)\n",
               size, (double)widest);
    }
    if (size < 1) size = 24;

    double lh = size * 1.2;
    double cy0 = (double)h / 2.0 - ((double)nlines - 1.0) * lh / 2.0;
    for (int i = 0; i < nlines; i++)
        gf_render(frame, lines[i], (float)w * 0.5f, (float)(cy0 + i * lh),
                  (float)size, 255, 255, 255);

    long long nframes = (long long)(dur * fps + 0.5);
    if (nframes < 1) nframes = 1;

    char outcmd[4096];
    FILE *enc;
    if (mute) {
        snprintf(outcmd, sizeof(outcmd),
                 "ffmpeg -v warning -y -f rawvideo -pix_fmt rgb24 "
                 "-s %dx%d -r %d -i - "
                 "-c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p "
                 "-t %.3f \"%s\"", w, h, fps, dur, out_mp4);
    } else {
        snprintf(outcmd, sizeof(outcmd),
                 "ffmpeg -v warning -y -f rawvideo -pix_fmt rgb24 "
                 "-s %dx%d -r %d -i - "
                 "-f lavfi -i \"anullsrc=r=48000:cl=stereo\" -shortest "
                 "-map 0:v:0 -map 1:a:0 "
                 "-c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p "
                 "-c:a aac -b:a 128k -t %.3f \"%s\"", w, h, fps, dur, out_mp4);
    }
    enc = popen(outcmd, "w");
    if (!enc) die("title: cannot start ffmpeg encoder");

    for (long long i = 0; i < nframes; i++)
        fwrite(frame->px, 1, (size_t)w * h * 3, enc);

    int rc = pclose(enc);
    frame_free(frame);
    if (rc != 0) fprintf(stderr, "gram title: encoder exited %d\n", rc);
    printf("title: wrote %s (%dx%d@%d, %d line(s), %.3fs, %s)\n",
           out_mp4, w, h, fps, nlines, dur, mute ? "video-only" : "with AAC bed");
    return rc != 0;
}