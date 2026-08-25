#include "slides.h"

#include "analysis.h"
#include "util.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ------------------------------------------------------------------ */
/* pool scanning                                                       */

static void scan_images(const char *dir, char ***out, int *n, int max)
{
    struct dirent **ents = NULL;
    int cnt = scandir(dir, &ents, NULL, alphasort);
    if (cnt < 0) die("slides: cannot scan image dir %s", dir);
    static const char *exts[] = { ".jpg", ".jpeg" };
    for (int i = 0; i < cnt; i++) {
        const char *name = ents[i]->d_name;
        if (name[0] == '.') { free(ents[i]); continue; }
        size_t len = strlen(name);
        int match = 0;
        for (size_t e = 0; e < sizeof(exts) / sizeof(exts[0]); e++) {
            size_t el = strlen(exts[e]);
            if (len > el && strcasecmp(name + len - el, exts[e]) == 0) {
                match = 1;
                break;
            }
        }
        if (match) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", dir, name);
            *out = xrealloc(*out, sizeof(char *) * (size_t)(*n + 1));
            (*out)[*n] = xstrdup(path);
            (*n)++;
            if (max > 0 && *n >= max) { free(ents[i]); break; }
        }
        free(ents[i]);
    }
    free(ents);
}

static void scan_audio(const char *dir, char ***out, int *n)
{
    struct dirent **ents = NULL;
    int cnt = scandir(dir, &ents, NULL, alphasort);
    if (cnt < 0) { return; }
    for (int i = 0; i < cnt; i++) {
        const char *name = ents[i]->d_name;
        if (name[0] != '.' && has_audio_ext(name)) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", dir, name);
            *out = xrealloc(*out, sizeof(char *) * (size_t)(*n + 1));
            (*out)[*n] = xstrdup(path);
            (*n)++;
        }
        free(ents[i]);
    }
    free(ents);
}

/* ------------------------------------------------------------------ */
/* decode jpeg -> rgb24, crop/fill, grayscale                          */

static int decode_image(const char *path, uint8_t *out, int W, int H)
{
    /* decode to raw RGB24 at target resolution */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "ffmpeg -v quiet -i \"%s\" -f rawvideo -pix_fmt rgb24 "
             "-vf scale=%d:%d -",
             path, W, H);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    size_t want = (size_t)W * H * 3;
    size_t got = fread(out, 1, want, fp);
    pclose(fp);
    return got == want ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* audio concatenation                                                  */

static void concat_audio(char const *const *paths, int n,
                         const char *out_path)
{
    char list_path[1024];
    snprintf(list_path, sizeof(list_path), "%s.concat.txt", out_path);
    FILE *fp = fopen(list_path, "w");
    if (!fp) die("slides: cannot write %s", list_path);
    for (int i = 0; i < n; i++) {
        char q[1024];
        sh_quote(q, sizeof(q), paths[i]);
        fprintf(fp, "file %s\n", q);
    }
    fclose(fp);

    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "ffmpeg -v warning -y -f concat -safe 0 -i \"%s\" "
             "-c:a pcm_s16le -ar 48000 -ac 2 \"%s\"",
             list_path, out_path);
    int rc = system(cmd);
    remove(list_path);
    if (rc != 0) die("slides: audio concat failed");
}

/* ------------------------------------------------------------------ */
/* entry point                                                         */

int slides_run(const char *img_dir, const char *fld_dir, const char *out_mp4,
               int w, int h, int fps, double dur, uint64_t seed,
               int max_images, int mute)
{
    if (!img_dir || !img_dir[0]) die("slides: --img DIR required");
    if (!out_mp4 || !out_mp4[0]) die("slides: output .mp4 path required");

    /* scan images */
    char **images = NULL;
    int nimg = 0;
    printf("slides: scanning %s\n", img_dir);
    scan_images(img_dir, &images, &nimg, max_images);
    if (nimg == 0) die("slides: no JPEG files found in %s", img_dir);
    printf("slides: %d image(s)\n", nimg);

    /* shuffle */
    rng_seed(seed);
    int *order = xmalloc(sizeof(int) * (size_t)nimg);
    for (int i = 0; i < nimg; i++) order[i] = i;
    shuffle_ints(order, nimg);

    /* decode + render frames */
    long long nframes = (long long)((double)nimg * dur * fps + 0.5);
    printf("slides: %d images, %.3fs each, %lld frames -> %s (%dx%d@%d)\n",
           nimg, dur, nframes, out_mp4, w, h, fps);

    /* encoder pipeline */
    char outcmd[4096];
    snprintf(outcmd, sizeof(outcmd),
             "ffmpeg -v warning -y -f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i - "
             "-c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p -an \"%s\"",
             w, h, fps, out_mp4);
    FILE *enc = popen(outcmd, "w");
    if (!enc) die("slides: cannot start ffmpeg encoder");

    uint8_t *frame = xmalloc((size_t)w * h * 3);
    long long fidx = 0;

    for (int i = 0; i < nimg && fidx < nframes; i++) {
        const char *path = images[order[i]];
        if (decode_image(path, frame, w, h) != 0) {
            fprintf(stderr, "slides: decode failed %s, using black\n", path);
            memset(frame, 0, (size_t)w * h * 3);
        }

        /* grayscale (BT.601 luma) */
        for (int p = 0; p < w * h; p++) {
            uint8_t r = frame[p * 3 + 0];
            uint8_t g = frame[p * 3 + 1];
            uint8_t b = frame[p * 3 + 2];
            uint8_t y = (uint8_t)(0.299f * r + 0.587f * g + 0.114f * b);
            frame[p * 3 + 0] = y;
            frame[p * 3 + 1] = y;
            frame[p * 3 + 2] = y;
        }

        long long span_frames = (long long)(dur * fps + 0.5);
        if (i == nimg - 1) span_frames = nframes - fidx;
        for (long long s = 0; s < span_frames && fidx < nframes; s++, fidx++)
            fwrite(frame, 1, (size_t)w * h * 3, enc);

        if (fidx % (fps * 10) == 0 || fidx >= nframes) {
            printf("\rslides: frame %lld/%lld (%.0f%%)", fidx, nframes,
                   100.0 * (double)fidx / (double)nframes);
            fflush(stdout);
        }
    }
    printf("\n");
    free(frame);

    int rc = pclose(enc);
    if (rc != 0) fprintf(stderr, "slides: encoder exited %d\n", rc);

    /* audio: concat field recordings and mux */
    if (!mute && fld_dir && fld_dir[0]) {
        char **audio = NULL;
        int naudio = 0;
        printf("slides: scanning field recordings %s\n", fld_dir);
        scan_audio(fld_dir, &audio, &naudio);
        if (naudio > 0) {
            printf("slides: %d field recording(s)\n", naudio);
            char tmp_wav[1024];
            snprintf(tmp_wav, sizeof(tmp_wav), "%s_slides_audio.wav", out_mp4);
            size_t blen = strlen(tmp_wav);
            /* remove .mp4 suffix if present, re-add _slides_audio.wav */
            if (blen > 4 && strcmp(tmp_wav + blen - 4, ".mp4") == 0)
                snprintf(tmp_wav + blen - 4, sizeof(tmp_wav) - (blen - 4),
                         "_slides_audio.wav");

            concat_audio((char const *const *)audio, naudio, tmp_wav);

            /* mux audio + video */
            char muxcmd[4096];
            snprintf(muxcmd, sizeof(muxcmd),
                     "ffmpeg -v warning -y -i \"%s\" -i \"%s\" "
                     "-c:v copy -c:a aac -b:a 192k -shortest \"%s\"",
                     out_mp4, tmp_wav, out_mp4);
            rc = system(muxcmd);
            remove(tmp_wav);
            if (rc != 0) fprintf(stderr, "slides: audio mux failed\n");
            else printf("slides: audio muxed -> %s\n", out_mp4);
            for (int i = 0; i < naudio; i++) free(audio[i]);
            free(audio);
        } else {
            printf("slides: no field recordings found in %s\n", fld_dir);
        }
    }

    for (int i = 0; i < nimg; i++) free(images[i]);
    free(images);
    free(order);
    return rc != 0;
}
