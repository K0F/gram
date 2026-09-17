#include "slides.h"

#include "analysis.h"
#include "gomfont.h"
#include "render.h"
#include "util.h"
#include "visual.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* pool scanning (recursive, alphasort-deterministic)                  */

static int has_img_ext(const char *name)
{
    static const char *exts[] = { ".jpg", ".jpeg", ".png" };
    size_t len = strlen(name);
    for (size_t e = 0; e < sizeof(exts) / sizeof(exts[0]); e++) {
        size_t el = strlen(exts[e]);
        if (len > el && strcasecmp(name + len - el, exts[e]) == 0) return 1;
    }
    return 0;
}

static void collect(const char *dir, char ***out, int *n, int max, int want_audio)
{
    struct dirent **ents = NULL;
    int cnt = scandir(dir, &ents, NULL, alphasort);
    if (cnt < 0) return;
    for (int i = 0; i < cnt; i++) {
        const char *name = ents[i]->d_name;
        if (name[0] == '.') { free(ents[i]); continue; }
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, name);
        if (ents[i]->d_type == DT_DIR) {
            collect(path, out, n, max, want_audio);
        } else {
            int match = want_audio ? has_audio_ext(name) : has_img_ext(name);
            if (match) {
                *out = xrealloc(*out, sizeof(char *) * (size_t)(*n + 1));
                (*out)[*n] = xstrdup(path);
                (*n)++;
                if (max > 0 && *n >= max) { free(ents[i]); break; }
            }
        }
        free(ents[i]);
    }
    free(ents);
}

static void scan_images(const char *dir, char ***out, int *n, int max)
{
    collect(dir, out, n, max, 0);
}

static void scan_audio(const char *dir, char ***out, int *n)
{
    collect(dir, out, n, 0, 1);
}

/* ------------------------------------------------------------------ */
/* dada oracle plumbing                                                */

static int file_exec(const char *path)
{
    return path && access(path, X_OK) == 0;
}
static int file_ok(const char *path)
{
    return path && access(path, F_OK) == 0;
}

static void exe_dir(char *buf, size_t n)
{
    buf[0] = '\0';
    ssize_t r = readlink("/proc/self/exe", buf, n - 1);
    if (r <= 0) return;
    buf[r] = '\0';
    char *s = strrchr(buf, '/');
    if (s) *s = '\0';
}

/* resolve the dada binary path (--dada-bin > $GRAM_DADA > exe/dada/dada
 * > cwd/dada/dada > "dada" on $PATH). */
static const char *resolve_dada_bin(const SlidesOpts *o, char *buf, size_t n)
{
    char ex[1024];
    exe_dir(ex, sizeof(ex));
    const char *cand[] = {
        o && o->dada_bin ? o->dada_bin : "",
        getenv("GRAM_DADA") ? getenv("GRAM_DADA") : "",
    };
    char tilde[1024];
    if (cand[0] && cand[0][0] == '~' && getenv("HOME")) {
        snprintf(tilde, sizeof(tilde), "%s%s", getenv("HOME"), cand[0] + 1);
        cand[0] = tilde;
    }
    for (int i = 0; i < 2; i++) {
        if (cand[i] && cand[i][0] && file_exec(cand[i])) {
            snprintf(buf, n, "%s", cand[i]);
            return buf;
        }
    }
    if (ex[0]) {
        snprintf(buf, n, "%s/dada/dada", ex);
        if (file_exec(buf)) return buf;
    }
    if (file_exec("dada/dada")) {
        snprintf(buf, n, "%s", "dada/dada");
        return buf;
    }
    const char *p = getenv("PATH");
    if (p) {
        char pbuf[4096];
        snprintf(pbuf, sizeof(pbuf), "%s", p);
        for (char *tok = strtok(pbuf, ":"); tok; tok = strtok(NULL, ":")) {
            snprintf(buf, n, "%s/dada", tok);
            if (file_exec(buf)) return buf;
        }
    }
    snprintf(buf, n, "dada");
    return buf;
}

/* resolve the stone image path (--stone > exe/dada/noise.png > dada/noise.png) */
static const char *resolve_stone(const SlidesOpts *o, char *buf, size_t n)
{
    char ex[1024];
    exe_dir(ex, sizeof(ex));
    if (o && o->stone && o->stone[0]) {
        if (file_ok(o->stone)) { snprintf(buf, n, "%s", o->stone); return buf; }
        char tilde[1024];
        if (o->stone[0] == '~' && getenv("HOME")) {
            snprintf(tilde, sizeof(tilde), "%s%s", getenv("HOME"), o->stone + 1);
            if (file_ok(tilde)) { snprintf(buf, n, "%s", tilde); return buf; }
        }
    }
    if (ex[0]) {
        snprintf(buf, n, "%s/dada/noise.png", ex);
        if (file_ok(buf)) return buf;
    }
    snprintf(buf, n, "dada/noise.png");
    return buf;
}

/* one 64-bit word of entropy for seed s: `dada --img STONE bytes 8 S` */
static uint64_t dada_bytes64(const char *stone, const char *bin, uint64_t seed)
{
    char cmd[4096], out[512] = "";
    char qb[1024], qs[1024];
    sh_quote(qb, sizeof(qb), bin);
    sh_quote(qs, sizeof(qs), stone);
    snprintf(cmd, sizeof(cmd), "%s --img %s bytes 8 %llu",
             qb, qs, (unsigned long long)seed);
    if (run_capture(cmd, out, sizeof(out)) != 0) {
        fprintf(stderr, "slides: dada call failed: %s\n", cmd);
        return seed * 6364136223846793005ULL;
    }
    for (char *p = out; *p; p++)
        if (*p == '\n' || *p == '\r') { *p = '\0'; break; }
    char *end = NULL;
    uint64_t v = strtoull(out, &end, 16);
    return end && end != out ? v : seed * 6364136223846793005ULL;
}

/* ------------------------------------------------------------------ */
/* decode -> crop/fill -> rgb24, then grayscale                        */

static int decode_image(const char *path, uint8_t *out, int W, int H)
{
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "ffmpeg -v quiet -i \"%s\" -f rawvideo -pix_fmt rgb24 "
             "-vf scale=%d:%d:force_original_aspect_ratio=increase,crop=%d:%d -",
             path, W, H, W, H);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    size_t want = (size_t)W * H * 3;
    size_t got = fread(out, 1, want, fp);
    pclose(fp);
    return got == want ? 0 : -1;
}

static void to_grayscale(uint8_t *px, int w, int h)
{
    for (int p = 0; p < w * h; p++) {
        uint8_t y = (uint8_t)(0.299f * px[p * 3 + 0] +
                              0.587f * px[p * 3 + 1] +
                              0.114f * px[p * 3 + 2]);
        px[p * 3 + 0] = y;
        px[p * 3 + 1] = y;
        px[p * 3 + 2] = y;
    }
}

/* ------------------------------------------------------------------ */
/* audio concatenation (rng mode)                                      */

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
/* dada-mode audio: deterministic dense field mix via the EDL engine   */

static int dense_field_mix(const char *fld_dir, const char *out_wav,
                           const char *stone, const char *dada_bin,
                           int total_sec)
{
    char **audio = NULL;
    int naudio = 0;
    scan_audio(fld_dir, &audio, &naudio);
    if (naudio == 0) {
        printf("slides: no field recordings found in %s\n", fld_dir);
        return -1;
    }

    /* render.c caps the EDL string at 2048 bytes, so keep slices small */
    int n = 12;
    char edl[2048];
    int off = 0;
    for (int i = 0; i < n && naudio > 0; i++) {
        uint64_t r = dada_bytes64(stone, dada_bin, 2000 + (uint64_t)i);
        int src = (int)(r % (uint64_t)naudio);
        double dur = source_duration_sec(audio[src]);
        if (dur < 2.0) continue;
        double span = 14.0 + (double)((r >> 8) % 15);
        if (span > dur * 0.7) span = dur * 0.7;
        double in = ((double)((r >> 16) % 4096) / 4096.0) *
                    (dur - span > 0 ? dur - span : 0);
        double at = ((double)i / (double)n) * total_sec +
                    ((double)((r >> 36) % 401) - 200.0) / 100.0;
        if (at < 0) at = 0;
        if (at + span > total_sec) at = total_sec - span;
        double fin = 2.0 + (double)((r >> 22) % 4);
        double fout = 2.0 + (double)((r >> 44) % 4);
        double vol = -9.0 - (double)((r >> 24) % 50) / 10.0;
        /* parse_track_spec takes the file path as the trailing token,
         * unquoted (like plan.c emits) */
        int w = snprintf(edl + off, sizeof(edl) - (size_t)off, "%sin%.1f out%.1f at%.1f v%.1f fin%.1f fout%.1f %s",
                         i ? "," : "", in, in + span, at, vol, fin, fout,
                         audio[src]);
        if (w < 0 || (size_t)w >= sizeof(edl) - (size_t)off) break;
        off += w;
    }
    if (off == 0) return -1;

    printf("slides: dense field mix (%d slices, %d files)\n", n, naudio);
    RenderOpts ro;
    render_opts_defaults(&ro);
    ro.master_mode = 1;
    snprintf(ro.master_graph, sizeof(ro.master_graph),
             "acompressor=threshold=-18dB:ratio=3:attack=20:release=250:makeup=1,"
             "stereotools=base=0.2,alimiter=limit=0.7071");
    if (render_edl(edl, out_wav, &ro) != 0) return -1;

    /* render_edl masters to <out>_master.wav when master_mode is on;
     * pad/trim that to exactly total_sec into the final wav */
    char master[1024];
    snprintf(master, sizeof(master), "%s", out_wav);
    size_t ml = strlen(master);
    if (ml > 4 && strcmp(master + ml - 4, ".wav") == 0)
        snprintf(master + ml - 4, sizeof(master) - (ml - 4), "_master.wav");
    else
        snprintf(master + ml, sizeof(master) - ml, "_master.wav");
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "ffmpeg -v warning -y -i \"%s\" -af apad -t %d "
             "-ar 48000 -ac 2 -c:a pcm_s16le \"%s.dense.wav\"",
             master, total_sec, out_wav);
    int rc = system(cmd);
    if (rc != 0) return -1;
    snprintf(cmd, sizeof(cmd), "mv -f \"%s.dense.wav\" \"%s\"", out_wav, out_wav);
    system(cmd);
    for (int i = 0; i < naudio; i++) free(audio[i]);
    free(audio);
    return 0;
}

/* ------------------------------------------------------------------ */
/* entry point                                                         */

int slides_run(const char *img_dir, const char *fld_dir, const char *out_mp4,
               int w, int h, int fps, double dur, uint64_t seed,
               int max_images, int mute, const SlidesOpts *opts)
{
    if (!img_dir || !img_dir[0]) die("slides: --img DIR required");
    if (!out_mp4 || !out_mp4[0]) die("slides: output .mp4 path required");
    int dada = opts && opts->dada;
    int title_slots = opts && opts->title_slots > 0 ? opts->title_slots : 0;

    setvbuf(stdout, NULL, _IOLBF, 0);

    char stone[1024] = "", dada_bin[1024] = "";
    if (dada) {
        resolve_dada_bin(opts, dada_bin, sizeof(dada_bin));
        resolve_stone(opts, stone, sizeof(stone));
        printf("slides: dada oracle %s  stone %s\n", dada_bin, stone);
    }

    /* scan images */
    char **images = NULL;
    int nimg = 0;
    printf("slides: scanning %s\n", img_dir);
    scan_images(img_dir, &images, &nimg, max_images);
    if (nimg == 0) die("slides: no pictures found in %s", img_dir);
    printf("slides: %d image(s)\n", nimg);

    long long nframes;
    double total_dur;
    int nslots;
    int *order = NULL;

    if (dada) {
        /* exact SLIDES_DADA_SEC run; title_slots lead, then pictures.
         * slide n's seed selects from the sorted pool. */
        int picslots = title_slots > 0
            ? (int)((SLIDES_DADA_SEC - title_slots * dur) / dur) + 1
            : (int)(SLIDES_DADA_SEC / dur);
        nslots = title_slots + picslots;
        nframes = (long long)(SLIDES_DADA_SEC * fps + 0.5);
        total_dur = SLIDES_DADA_SEC;
        printf("slides: dada film %d slots (%d title + %d pics), %.0f frames -> %s (%dx%d@%d)\n",
               nslots, title_slots, picslots, (double)nframes, out_mp4, w, h, fps);
    } else {
        rng_seed(seed);
        order = xmalloc(sizeof(int) * (size_t)nimg);
        for (int i = 0; i < nimg; i++) order[i] = i;
        shuffle_ints(order, nimg);
        nslots = nimg;
        nframes = (long long)((double)nimg * dur * fps + 0.5);
        total_dur = (double)nimg * dur;
        printf("slides: %d images, %.3fs each, %lld frames -> %s (%dx%d@%d)\n",
               nimg, dur, (long long)nframes, out_mp4, w, h, fps);
    }

    /* encoder pipeline — -shortest stops at last frame, no repeats */
    char outcmd[4096];
    snprintf(outcmd, sizeof(outcmd),
             "ffmpeg -v warning -y -f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i - "
             "-c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p -an "
             "-t %.3f \"%s\"",
             w, h, fps, total_dur, out_mp4);
    FILE *enc = popen(outcmd, "w");
    if (!enc) die("slides: cannot start ffmpeg encoder");

    uint8_t *frame = xmalloc((size_t)w * h * 3);
    long long fidx = 0;

    /* decoded-grayscale cache keyed by pool index (dada mode): large film
     * scans decode slowly, so decode each unique image at most once */
    uint8_t **gcache = NULL;   /* w*h rows, 0xFF-sentinel via flag */
    char *gcused = NULL;
    if (dada) {
        gcache = xmalloc(sizeof(uint8_t *) * (size_t)nimg);
        gcused = xmalloc((size_t)nimg);
        memset(gcused, 0, (size_t)nimg);
    }
    uint8_t *title_frame = NULL;
    int title_decoded = 0;

    for (int slot = 0; slot < nslots && fidx < nframes; slot++) {
        const char *path = NULL;
        uint8_t *srcpx = frame;
        int is_title = 0;
        int idx = -1;

        if (dada) {
            if (slot < title_slots) {
                is_title = 1;
            } else {
                uint64_t b = dada_bytes64(stone, dada_bin, (uint64_t)slot);
                idx = (int)(b % (uint64_t)nimg);
                path = images[idx];
            }
        } else {
            idx = order[slot];
            path = images[idx];
        }

        if (is_title) {
            if (!title_decoded) {
                title_frame = xmalloc((size_t)w * h * 3);
                if (opts->title && opts->title[0] && file_ok(stone)) {
                    if (decode_image(stone, title_frame, w, h) == 0) {
                        to_grayscale(title_frame, w, h);
                        Frame f = { w, h, title_frame };
                        gf_render(&f, opts->title,
                                  w * 0.5f, h * 0.5f,
                                  opts->title_px > 0 ? (float)opts->title_px : 12.0f,
                                  255, 255, 255);
                        title_decoded = 1;
                    }
                }
                if (!title_decoded) {
                    memset(title_frame, 0, (size_t)w * h * 3);
                    title_decoded = 1;
                }
            }
            srcpx = title_frame;
        } else if (gcache) {
            if (!gcused[idx]) {
                gcache[idx] = xmalloc((size_t)w * h);
                if (path && decode_image(path, frame, w, h) == 0) {
                    to_grayscale(frame, w, h);
                    for (int p = 0; p < w * h; p++)
                        gcache[idx][p] = frame[p * 3 + 0];
                    gcused[idx] = 1;
                }
            }
            if (gcused[idx]) {
                uint8_t *gsrc = gcache[idx];
                for (int p = 0; p < w * h; p++) {
                    uint8_t y = gsrc[p];
                    frame[p * 3 + 0] = y;
                    frame[p * 3 + 1] = y;
                    frame[p * 3 + 2] = y;
                }
            } else {
                fprintf(stderr, "slides: decode failed %s, using black\n", path);
                memset(frame, 0, (size_t)w * h * 3);
            }
            srcpx = frame;
        } else {
            if (!path || decode_image(path, frame, w, h) != 0) {
                fprintf(stderr, "slides: decode failed %s, using black\n", path);
                memset(frame, 0, (size_t)w * h * 3);
            }
            to_grayscale(frame, w, h);
        }

        /* distribute frames so the total lands exactly on nframes */
        long long base = nframes / nslots;
        long long rem = nframes % nslots;
        long long span_frames = base + (slot < rem ? 1 : 0);
        for (long long s = 0; s < span_frames && fidx < nframes; s++, fidx++)
            fwrite(srcpx, 1, (size_t)w * h * 3, enc);

        if (fidx % (fps * 10) == 0 || fidx >= nframes) {
            printf("\rslides: frame %lld/%lld (%.0f%%)  [slot %d/%d]  ",
                   (long long)fidx, (long long)nframes,
                   100.0 * (double)fidx / (double)nframes, slot + 1, nslots);
            fflush(stdout);
        }
    }
    printf("\n");
    free(frame);
    free(title_frame);
    if (gcache) {
        for (int i = 0; i < nimg; i++)
            if (gcused[i]) free(gcache[i]);
        free(gcache);
        free(gcused);
    }

    int rc = pclose(enc);
    if (rc != 0) fprintf(stderr, "slides: encoder exited %d\n", rc);

    /* audio */
    if (!mute && fld_dir && fld_dir[0]) {
        char tmp_wav[1024];
        snprintf(tmp_wav, sizeof(tmp_wav), "%s", out_mp4);
        size_t blen = strlen(tmp_wav);
        if (blen > 4 && strcmp(tmp_wav + blen - 4, ".mp4") == 0)
            snprintf(tmp_wav + blen - 4, sizeof(tmp_wav) - (blen - 4),
                     "_slides_audio.wav");

        if (dada) {
            printf("slides: field recordings %s\n", fld_dir);
            if (dense_field_mix(fld_dir, tmp_wav, stone, dada_bin,
                                (int)total_dur) == 0) {
                char muxtmp[1024];
                snprintf(muxtmp, sizeof(muxtmp), "%s.tmp.mp4", out_mp4);
                char muxcmd[4096];
                snprintf(muxcmd, sizeof(muxcmd),
                         "ffmpeg -v warning -y -i \"%s\" -i \"%s\" "
                         "-c:v copy -c:a aac -b:a 192k -shortest \"%s\"",
                         out_mp4, tmp_wav, muxtmp);
                rc = system(muxcmd);
                remove(tmp_wav);
                size_t twl = strlen(tmp_wav);
                if (twl > 4 && strcmp(tmp_wav + twl - 4, ".wav") == 0) {
                    snprintf(tmp_wav + twl - 4, sizeof(tmp_wav) - (twl - 4), "_master.wav");
                    remove(tmp_wav);
                }
                if (rc != 0) {
                    fprintf(stderr, "slides: audio mux failed\n");
                    remove(muxtmp);
                } else {
                    if (rename(muxtmp, out_mp4) != 0)
                        fprintf(stderr, "slides: failed to rename mux output\n");
                    else
                        printf("slides: audio muxed -> %s\n", out_mp4);
                }
            }
        } else {
            char **audio = NULL;
            int naudio = 0;
            printf("slides: scanning field recordings %s\n", fld_dir);
            scan_audio(fld_dir, &audio, &naudio);
            if (naudio > 0) {
                printf("slides: %d field recording(s)\n", naudio);
                concat_audio((char const *const *)audio, naudio, tmp_wav);

                char muxtmp[1024];
                snprintf(muxtmp, sizeof(muxtmp), "%s.tmp.mp4", out_mp4);
                char muxcmd[4096];
                snprintf(muxcmd, sizeof(muxcmd),
                         "ffmpeg -v warning -y -i \"%s\" -i \"%s\" "
                         "-c:v copy -c:a aac -b:a 192k -shortest \"%s\"",
                         out_mp4, tmp_wav, muxtmp);
                rc = system(muxcmd);
                remove(tmp_wav);
                if (rc != 0) {
                    fprintf(stderr, "slides: audio mux failed\n");
                    remove(muxtmp);
                } else {
                    if (rename(muxtmp, out_mp4) != 0)
                        fprintf(stderr, "slides: failed to rename mux output\n");
                    else
                        printf("slides: audio muxed -> %s\n", out_mp4);
                }
                for (int i = 0; i < naudio; i++) free(audio[i]);
                free(audio);
            } else {
                printf("slides: no field recordings found in %s\n", fld_dir);
            }
        }
    }

    for (int i = 0; i < nimg; i++) free(images[i]);
    free(images);
    free(order);
    return rc != 0;
}