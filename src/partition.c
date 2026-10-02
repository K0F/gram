#include "partition.h"

#include "analysis.h"
#include "av_render.h"
#include "util.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PART_DEFAULT_UNIT 11
#define PART_MAX_SEGS 8192
#define PART_MAX_TOKS 65536
#define PART_DA_N 233
#define PART_DA_D 144

/* ------------------------------------------------------------------ */
/* lexer (port of scripts/lexer.awk)                                   */

static int lex_push(PTok *toks, int cap, int *n, int type, long long num,
                    const char *word)
{
    if (*n >= cap) return -1;
    toks[*n].type = type;
    toks[*n].num = num;
    toks[*n].word[0] = 0;
    if (word) snprintf(toks[*n].word, sizeof(toks[*n].word), "%s", word);
    (*n)++;
    return 0;
}

static int lex_classify(PTok *toks, int cap, int *n, const char *w, int wtype)
{
    if (!wtype) return 0;
    if (wtype == 2) return lex_push(toks, cap, n, PT_NUM, atoll(w), NULL);
    if (strcmp(w, "x") == 0) return lex_push(toks, cap, n, PT_X, 0, NULL);
    if (strcmp(w, "h") == 0) return lex_push(toks, cap, n, PT_KW, 0, "H");
    if (strcmp(w, "jf") == 0 || strcmp(w, "jb") == 0 || strcmp(w, "pf") == 0)
        return lex_push(toks, cap, n, PT_VERB, 0, w);
    if (strcmp(w, "open") == 0 || strcmp(w, "file") == 0)
        return lex_push(toks, cap, n, PT_VERB, 0, "open");
    return lex_push(toks, cap, n, PT_ERR, 0, w);
}

int part_lex(const char *text, PTok *toks, int cap)
{
    int n = 0;
    char word[64] = { 0 };
    int wl = 0, wtype = 0; /* 0 none, 1 letters, 2 digits */
    if (cap < 2) return -1;

    for (const char *p = text; *p; p++) {
        char c = *p;
        if (c == '\n') {
            if (lex_classify(toks, cap, &n, word, wtype) != 0) return -1;
            wl = 0; wtype = 0; word[0] = 0;
            continue;
        }
        if (c == '#') {
            if (lex_classify(toks, cap, &n, word, wtype) != 0) return -1;
            wl = 0; wtype = 0; word[0] = 0;
            while (*p && *p != '\n') p++;
            if (*p == '\n') continue;
            break;
        }
        int letter = (c >= 'a' && c <= 'z') ? 1 : (c >= 'A' && c <= 'Z') ? 2 : 0;
        int digit = c >= '0' && c <= '9';
        if (letter || digit) {
            char wc = digit ? c : (char)(c | 0x20);
            int ntype = digit ? 2 : 1;
            if (wtype && wtype != ntype) {
                if (lex_classify(toks, cap, &n, word, wtype) != 0) return -1;
                wl = 0; wtype = 0; word[0] = 0;
            }
            if (!wtype) wtype = ntype;
            if (wl < 63) { word[wl++] = wc; word[wl] = 0; }
        } else {
            if (wtype) {
                if (lex_classify(toks, cap, &n, word, wtype) != 0) return -1;
                wl = 0; wtype = 0; word[0] = 0;
            }
        }
    }
    if (wtype)
        if (lex_classify(toks, cap, &n, word, wtype) != 0) return -1;
    if (lex_push(toks, cap, &n, PT_EOF, 0, NULL) != 0) return -1;
    return n;
}

/* ------------------------------------------------------------------ */
/* interpreter (port of scripts/interp.awk)                            */

static void interp_push(PSeg *seg, int cap, int *n, long long b, long long e,
                        int *trunc)
{
    if (*n >= cap) { *trunc = 1; return; }
    seg[(*n)++] = (PSeg){ b, e };
}

int part_interp(const PTok *toks, int ntok,
                const long long *lens, const long long *full,
                const long long *offs, int nf, int require_open,
                long long unit, PSeg *seg, int segcap)
{
    if (unit < 1) unit = 10;
    int nseg = 0, trunc = 0;
    int fidx = require_open ? -1 : 0;
    long long t = 0;
    int i = 0;

    while (i < ntok && !trunc) {
        if (toks[i].type == PT_EOF) break;

        if (toks[i].type == PT_VERB && strcmp(toks[i].word, "open") == 0) {
            i++;
            if (i < ntok && toks[i].type == PT_NUM) {
                int idx = (int)toks[i].num; i++;
                if (idx < 0 || idx >= nf) {
                    fprintf(stderr, "partition: 'open' index %d out of range 0..%d\n",
                            idx, nf - 1);
                    return -1;
                }
                fidx = idx;
                t = 0;
                continue;
            }
            fprintf(stderr, "partition: missing source index after 'open'\n");
            return -1;
        }

        if (toks[i].type == PT_KW) { /* H n m */
            if (fidx < 0) { fprintf(stderr, "partition: no file open\n"); return -1; }
            i++;
            if (!(i < ntok && toks[i].type == PT_NUM)) {
                fprintf(stderr, "partition: missing section count after H\n"); return -1;
            }
            long long nsec = toks[i].num; i++;
            if (!(i < ntok && toks[i].type == PT_NUM)) {
                fprintf(stderr, "partition: missing play units after H\n"); return -1;
            }
            long long play = toks[i].num; i++;
            long long n = lens[fidx], ffull = full[fidx];
            if (nsec >= 1 && play >= 1) {
                double sum = 0.0;
                for (long long k = 1; k <= nsec; k++) sum += 1.0 / (double)k;
                long long total_cuts = nsec * play;
                for (long long j = 0; j < total_cuts && !trunc; j++) {
                    double frac = ((double)j + 0.5) / (double)total_cuts;
                    double cum = 0.0;
                    for (long long k = 1; k <= nsec; k++) {
                        cum += (1.0 / (double)k) / sum;
                        if (frac <= cum) break;
                    }
                    long long pos = (long long)((double)ffull * frac + 0.5);
                    if (pos >= n) pos = n - unit;
                    if (pos < 0) pos = 0;
                    long long s = pos, e = s + unit;
                    if (e > n) e = n;
                    if (e > s) interp_push(seg, segcap, &nseg,
                                           offs[fidx] + s, offs[fidx] + e, &trunc);
                }
            }
            continue;
        }

        if (toks[i].type == PT_VERB) {
            if (fidx < 0) { fprintf(stderr, "partition: no file open\n"); return -1; }
            const char *verb = toks[i].word; i++;
            if (!(i < ntok && toks[i].type == PT_NUM)) {
                fprintf(stderr, "partition: missing count after '%s'\n", verb);
                return -1;
            }
            long long count = toks[i].num; i++;
            long long times = 1;
            if (i < ntok && toks[i].type == PT_X) {
                i++;
                if (!(i < ntok && toks[i].type == PT_NUM)) {
                    fprintf(stderr, "partition: missing repeat count after x\n");
                    return -1;
                }
                times = toks[i].num; i++;
            }
            if (times < 1) times = 1;
            long long n = lens[fidx];
            long long step = count * unit;
            for (long long r = 0; r < times && !trunc; r++) {
                if (strcmp(verb, "jf") == 0) {
                    t += step;
                } else if (strcmp(verb, "jb") == 0) {
                    t -= step;
                    if (t < 0) t = 0;
                } else if (strcmp(verb, "pf") == 0) {
                    if (t >= n) continue;
                    long long b = t + step;
                    if (b > n) b = n;
                    if (b > t) {
                        interp_push(seg, segcap, &nseg,
                                    offs[fidx] + t, offs[fidx] + b, &trunc);
                        t = b;
                    }
                }
            }
            continue;
        }

        fprintf(stderr, "partition: unexpected token '%s'\n", toks[i].word);
        return -1;
    }

    if (trunc)
        fprintf(stderr, "partition: segment capacity %d reached; output truncated\n",
                segcap);
    return nseg;
}

/* ------------------------------------------------------------------ */
/* script source discovery                                             */

static char *strip_path(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *buf = xstrdup(s);
    char *p = strstr(buf, " #");
    if (p) *p = 0;
    size_t l = strlen(buf);
    while (l > 0 && (buf[l-1] == ' ' || buf[l-1] == '\t' ||
                     buf[l-1] == '\n' || buf[l-1] == '\r'))
        buf[--l] = 0;
    return buf;
}

static char *read_whole_file(const char *path, size_t *len_out)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    if (sz < 0) { fclose(fp); return NULL; }
    rewind(fp);
    char *buf = xmalloc((size_t)sz + 1);
    size_t got = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[got] = 0;
    if (len_out) *len_out = got;
    return buf;
}

static char *script_first_word(const char *line)
{
    while (*line == ' ' || *line == '\t') line++;
    static char word[32];
    int i = 0;
    while ((*line == ' ' || *line == '\t') == 0 && *line && *line != '\n' && i < 31)
        word[i++] = (char)tolower((unsigned char)*line++);
    word[i] = 0;
    return word;
}

int part_script_open_sources(const char *script, char ***out, int *n)
{
    size_t sz = 0;
    char *buf = read_whole_file(script, &sz);
    if (!buf) { fprintf(stderr, "partition: cannot open script %s\n", script); return -1; }
    char **list = NULL;
    int cnt = 0;
    const char *p = buf;
    while (p && *p) {
        const char *eol = strchr(p, '\n');
        size_t flen = eol ? (size_t)(eol - p) : strlen(p);
        char line[1200];
        if (flen >= sizeof(line)) flen = sizeof(line) - 1;
        memcpy(line, p, flen); line[flen] = 0;
        p = eol ? eol + 1 : p + flen;
        char *w = script_first_word(line);
        if (strcmp(w, "open") != 0 && strcmp(w, "file") != 0) continue;
        char trimmed_fixed[1200];
        snprintf(trimmed_fixed, sizeof(trimmed_fixed), "%s", line + (int)strlen(w));
        char *path = strip_path(trimmed_fixed);
        if (!path[0]) { free(path); continue; }
        char *rp = realpath(path, NULL);
        if (!rp) rp = xstrdup(path);
        free(path);
        int dup = 0;
        for (int i = 0; i < cnt; i++)
            if (strcmp(list[i], rp) == 0) { dup = 1; break; }
        if (!dup) {
            list = xrealloc(list, sizeof(char *) * (size_t)(cnt + 1));
            list[cnt++] = rp;
        } else {
            free(rp);
        }
    }
    free(buf);
    *out = list;
    *n = cnt;
    return cnt > 0 ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* plan                                                                */

int part_plan(const char *pattern, const char *script,
              const char *const *srcs, int nsrcs, int unit, double fps,
              PCut **cuts_out, int *ncuts_out, double *total_sec_out)
{
    if (!srcs || nsrcs < 1) {
        fprintf(stderr, "partition: no sources (-i FILE)\n");
        return -1;
    }
    if ((!pattern || !pattern[0]) && (!script || !script[0])) {
        fprintf(stderr, "partition: -p pattern or --script required\n");
        return -1;
    }
    if (fps <= 0) fps = 25.0;
    long long u = unit > 0 ? unit : PART_DEFAULT_UNIT;
    int use_script = script && script[0];

    long long *lens = xmalloc(sizeof(long long) * (size_t)nsrcs);
    long long *full = xmalloc(sizeof(long long) * (size_t)nsrcs);
    long long *offs = xmalloc(sizeof(long long) * (size_t)nsrcs);
    long long total = 0;
    for (int i = 0; i < nsrcs; i++) {
        double dur = source_duration_sec(srcs[i]);
        if (dur <= 0) {
            fprintf(stderr, "partition: cannot probe %s (assuming 60s)\n", srcs[i]);
            dur = 60.0;
        }
        long long l = (long long)(dur * fps + 0.5);
        if (l < 1) l = 1;
        lens[i] = l;
        full[i] = l;
        offs[i] = total;
        total += l;
    }

    char *body = NULL;
    if (use_script) {
        size_t sz = 0;
        char *raw = read_whole_file(script, &sz);
        if (!raw) {
            fprintf(stderr, "partition: cannot open script %s\n", script);
            free(lens); free(full); free(offs);
            return -1;
        }
        /* rewrite 'open <path>' -> 'open <idx>' so paths need no lexing */
        size_t cap = sz + 1, len = 0;
        body = xmalloc(cap);
        const char *p = raw;
        while (p && *p) {
            const char *eol = strchr(p, '\n');
            size_t flen = eol ? (size_t)(eol - p) : strlen(p);
            char line[1200];
            if (flen >= sizeof(line)) flen = sizeof(line) - 1;
            memcpy(line, p, flen); line[flen] = 0;
            p = eol ? eol + 1 : p + flen;
            char *w = script_first_word(line);
            if (strcmp(w, "open") == 0 || strcmp(w, "file") == 0) {
                char tf[1200];
                snprintf(tf, sizeof(tf), "%s", line + (int)strlen(w));
                char *path = strip_path(tf);
                char *rp = realpath(path, NULL);
                if (!rp) rp = xstrdup(path);
                free(path);
                int idx = -1;
                for (int j = 0; j < nsrcs; j++)
                    if (strcmp(rp, srcs[j]) == 0) { idx = j; break; }
                free(rp);
                if (idx < 0) {
                    fprintf(stderr, "partition: '%s' not among -i sources\n", tf);
                    free(raw); free(body); free(lens); free(full); free(offs);
                    return -1;
                }
                while (len + 32 > cap) { cap *= 2; body = xrealloc(body, cap); }
                len += (size_t)snprintf(body + len, cap - len, "open %d\n", idx);
            } else {
                size_t n = flen + 1;
                while (len + n > cap) { cap *= 2; body = xrealloc(body, cap); }
                memcpy(body + len, line, flen);
                len += flen;
                body[len++] = '\n';
            }
        }
        body[len] = 0;
        free(raw);
    } else {
        body = xstrdup(pattern);
    }

    PTok *toks = xmalloc(sizeof(PTok) * PART_MAX_TOKS);
    int ntok = part_lex(body, toks, PART_MAX_TOKS);
    free(body);
    if (ntok < 0) {
        fprintf(stderr, "partition: pattern too long\n");
        free(toks); free(lens); free(full); free(offs);
        return -1;
    }
    int has_err = 0;
    for (int i = 0; i < ntok; i++) {
        if (toks[i].type == PT_ERR) {
            fprintf(stderr, "partition: pattern lex error: '%s'\n", toks[i].word);
            has_err = 1;
        }
    }
    if (has_err) {
        free(toks); free(lens); free(full); free(offs);
        return -1;
    }

    const long long *clens = lens, *cfull = full, *coffs = offs;
    int cnf = nsrcs;
    long long single_l = 0, single_f = 0, single_o = 0;
    if (!use_script) {
        single_l = total; single_f = total; single_o = 0; cnf = 1;
        clens = &single_l; cfull = &single_f; coffs = &single_o;
    }

    PSeg *segs = xmalloc(sizeof(PSeg) * PART_MAX_SEGS);
    int ns = part_interp(toks, ntok, clens, cfull, coffs, cnf,
                         use_script ? 1 : 0, u, segs, PART_MAX_SEGS);
    free(toks);
    if (ns < 0) { free(segs); free(lens); free(full); free(offs); return -1; }
    if (ns == 0) {
        fprintf(stderr, "partition: pattern produced no play segments\n");
        free(segs); free(lens); free(full); free(offs);
        return -1;
    }

    PCut *cuts = xmalloc(sizeof(PCut) * (size_t)ns);
    long long out_frames = 0;
    for (int j = 0; j < ns; j++) {
        long long b = segs[j].b, e = segs[j].e;
        int s = -1;
        for (int k = 0; k < nsrcs; k++)
            if (b >= offs[k] && b < offs[k] + lens[k]) { s = k; break; }
        if (s < 0)
            for (int k = nsrcs - 1; k >= 0; k--)
                if (b >= offs[k]) { s = k; break; }
        if (s < 0) {
            free(segs); free(cuts); free(lens); free(full); free(offs);
            return -1;
        }
        cuts[j].src = s;
        cuts[j].local = b - offs[s];
        cuts[j].len = e - b;
        out_frames += cuts[j].len;
    }
    free(segs);
    free(lens); free(full); free(offs);

    if (cuts_out) *cuts_out = cuts; else free(cuts);
    if (ncuts_out) *ncuts_out = ns;
    if (total_sec_out) *total_sec_out = (double)out_frames / fps;
    printf("partition: %d source(s), %lld combined frames, %d cut(s), "
           "%.3fs output @ %.0ffps (unit %lld)\n",
           nsrcs, (long long)total, ns, (double)out_frames / fps, fps, u);
    return 0;
}

/* ------------------------------------------------------------------ */
/* outputs                                                             */

int part_write_edl(const PCut *cuts, int ncuts,
                   const char *const *srcs, int nsrcs,
                   const char *path, int fps)
{
    if (fps <= 0) fps = 25;
    FILE *fp = fopen(path, "w");
    if (!fp) { fprintf(stderr, "partition: cannot write %s\n", path); return -1; }
    fprintf(fp, "# mpv EDL v2 (gram partition)\n");
    for (int i = 0; i < ncuts; i++) {
        const PCut *c = &cuts[i];
        if (c->src < 0 || c->src >= nsrcs) continue;
        fprintf(fp, "%s,%g,%g\n",
                srcs[c->src], (double)c->local / fps,
                (double)(c->local + c->len) / fps);
    }
    fclose(fp);
    printf("partition: wrote %s\n", path);
    return 0;
}

/* normalize a source to WxH@fps, cached (port of partition.sh --std) */
static int part_normalize_src(const char *src, int W, int H, int fps,
                              char *cache_out, size_t cache_out_n)
{
    char probe[4096], out[8192];
    snprintf(probe, sizeof(probe),
             "ffprobe -v error -show_entries stream=width,height,r_frame_rate,"
             "sample_aspect_ratio -show_entries format=duration "
             "-of default=noprint_wrappers=1 \"%s\" 2>/dev/null", src);
    if (run_capture(probe, out, sizeof(out)) != 0 || !out[0]) {
        fprintf(stderr, "partition: cannot probe %s\n", src);
        return -1;
    }
    int w = 0, h = 0, fn = 0, fd = 0, sn = 1, sd = 1;
    double dur = 0.0;
    char *ctx = NULL;
    for (char *l = strtok_r(out, "\n", &ctx); l; l = strtok_r(NULL, "\n", &ctx)) {
        if (strncmp(l, "width=", 6) == 0) w = atoi(l + 6);
        else if (strncmp(l, "height=", 7) == 0) h = atoi(l + 7);
        else if (strncmp(l, "r_frame_rate=", 13) == 0) {
            const char *v = l + 13;
            char *slash = strchr(v, '/');
            if (slash) { fn = atoi(v); fd = atoi(slash + 1); }
            else { fn = atoi(v); fd = 1; }
        } else if (strncmp(l, "sample_aspect_ratio=", 20) == 0) {
            if (sscanf(l + 20, "%d:%d", &sn, &sd) != 2) { sn = 1; sd = 1; }
        } else if (strncmp(l, "duration=", 9) == 0) dur = atof(l + 9);
    }
    if (w < 1 || h < 1 || fd <= 0) {
        fprintf(stderr, "partition: bad probe for %s\n", src);
        return -1;
    }
    if (dur <= 0) dur = source_duration_sec(src);
    if (dur <= 0) dur = 60.0;

    if (w == W && h == H && fn == fps && fd == 1 && sn == 1 && sd == 1) {
        snprintf(cache_out, cache_out_n, "%s", src);
        return 0;
    }

    int sw, sh, cx, cy;
    if ((long long)w * sn * H >= (long long)W * h * sd) {
        sw = (int)(((double)H * w * sn + (double)h * sd - 1) / ((double)h * sd));
        if (sw % 2) sw++;
        sh = H; cx = (sw - W) / 2; cy = 0;
    } else {
        sh = (int)(((double)W * h * sd + (double)w * sn - 1) / ((double)w * sn));
        if (sh % 2) sh++;
        sw = W; cx = 0; cy = (sh - H) / 2;
    }

    struct stat st;
    long size_k = -1, mtime_k = -1;
    if (stat(src, &st) == 0) { size_k = (long)st.st_size; mtime_k = (long)st.st_mtime; }

    char keybuf[2048];
    snprintf(keybuf, sizeof(keybuf), "%s|%ld|%ld|%d|%d|%d/%d|%d:%d|%.6f|%d%d%d%d",
             src, size_k, mtime_k, w, h, fn, fd, sn, sd, dur, sw, sh, cx, cy);
    char key[32];
    snprintf(key, sizeof(key), "%08lx", fnv1a(keybuf));

    const char *xdg = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");
    char cache_dir[1024];
    if (xdg && xdg[0])
        snprintf(cache_dir, sizeof(cache_dir), "%s/partition_std", xdg);
    else
        snprintf(cache_dir, sizeof(cache_dir), "%s/.cache/partition_std", home ? home : ".");
    mkdir(cache_dir, 0755);

    snprintf(cache_out, cache_out_n, "%s/%s.mp4", cache_dir, key);
    if (access(cache_out, F_OK) == 0) {
        double cdur = source_duration_sec(cache_out);
        if (cdur > 0 && cdur > dur - 1.5 && cdur < dur + 1.5) return 0;
    }

    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "ffmpeg -v error -y -i \"%s\" -map 0:v:0 -map 0:a:0? -t %.3f "
             "-vf \"scale=%d:%d,crop=%d:%d:%d:%d,fps=%d,format=yuv420p\" "
             "-c:v libx264 -preset fast -crf 18 "
             "-c:a aac -b:a 192k -ar 48000 -ac 2 -f mp4 \"%s\"",
             src, dur, sw, sh, cx, cy, W, H, fps, cache_out);
    printf("partition: standardizing %s -> %dx%d@%d (%s)\n", src, W, H, fps, cache_out);
    if (system(cmd) != 0) {
        fprintf(stderr, "partition: ffmpeg standardization failed for %s\n", src);
        return -1;
    }
    return 0;
}

static void root_of(const char *path, char *out, size_t n)
{
    snprintf(out, n, "%s", path);
    char *slash = strrchr(out, '/');
    if (slash) {
        if (slash == out) slash[1] = 0;
        else *slash = 0;
    } else {
        snprintf(out, n, ".");
    }
}

int part_write_mlt(const PCut *cuts, int ncuts,
                   const char *const *srcs, int nsrcs,
                   const char *path, int w, int h, int fps, int std)
{
    if (w <= 0 || h <= 0 || fps <= 0) { w = 932; h = 576; fps = 25; }
    char **res = xmalloc(sizeof(char *) * (size_t)nsrcs);
    long long *frames = xmalloc(sizeof(long long) * (size_t)nsrcs);
    for (int i = 0; i < nsrcs; i++) {
        frames[i] = (long long)(source_duration_sec(srcs[i]) * fps + 0.5);
        if (frames[i] < 1) frames[i] = 1;
        char buf[2048];
        if (std) {
            if (part_normalize_src(srcs[i], w, h, fps, buf, sizeof(buf))) {
                for (int j = 0; j < i; j++) free(res[j]);
                free(res); free(frames);
                return -1;
            }
        } else {
            snprintf(buf, sizeof(buf), "%s", srcs[i]);
        }
        res[i] = xstrdup(buf);
    }

    char root[1024];
    root_of(srcs[0], root, sizeof(root));

    long long out_frames = 0;
    for (int i = 0; i < ncuts; i++)
        if (cuts[i].src >= 0 && cuts[i].src < nsrcs) out_frames += cuts[i].len;
    if (out_frames < 1) out_frames = 1;

    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "partition: cannot write %s\n", path);
        for (int j = 0; j < nsrcs; j++) free(res[j]);
        free(res); free(frames);
        return -1;
    }

    fprintf(fp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(fp, "<mlt LC_NUMERIC=\"C\" version=\"7.40.0\" root=\"%s\">\n", root);
    fprintf(fp, "  <profile description=\"structural\" width=\"%d\" height=\"%d\" "
                "progressive=\"1\" sample_aspect_num=\"1\" sample_aspect_den=\"1\" "
                "display_aspect_num=\"%d\" display_aspect_den=\"%d\" "
                "frame_rate_num=\"%d\" frame_rate_den=\"1\" colorspace=\"709\"/>\n",
            w, h, PART_DA_N, PART_DA_D, fps);

    size_t rl = strlen(root);
    for (int i = 0; i < nsrcs; i++) {
        const char *r = res[i];
        const char *rel = r;
        if (rl > 0 && strncmp(r, root, rl) == 0 && r[rl] == '/') rel = r + rl + 1;
        fprintf(fp, "  <producer id=\"src%d\" in=\"0\" out=\"%lld\">\n"
                    "    <property name=\"resource\">%s</property>\n"
                    "  </producer>\n",
                i, frames[i] - 1, rel);
    }

    fprintf(fp, "  <playlist id=\"main\">\n");
    for (int i = 0; i < ncuts; i++) {
        const PCut *c = &cuts[i];
        if (c->src < 0 || c->src >= nsrcs) continue;
        if (c->len < 1) continue;
        fprintf(fp, "    <entry producer=\"src%d\" in=\"%lld\" out=\"%lld\"/>\n",
                c->src, c->local, c->local + c->len - 1);
    }
    fprintf(fp, "  </playlist>\n");
    fprintf(fp, "  <tractor id=\"t0\" in=\"0\" out=\"%lld\">\n", out_frames - 1);
    fprintf(fp, "    <track producer=\"main\"/>\n");
    fprintf(fp, "  </tractor>\n");
    fprintf(fp, "</mlt>\n");

    fclose(fp);
    for (int j = 0; j < nsrcs; j++) free(res[j]);
    free(res);
    free(frames);
    printf("partition: wrote %s (%lld output frames, %d cuts)\n",
           path, (long long)out_frames, ncuts);
    return 0;
}

int part_render(const PCut *cuts, int ncuts,
                const char *const *srcs, int nsrcs,
                const char *out_mp4, int w, int h, int fps)
{
    AvOpts o;
    av_opts_defaults(&o);
    if (w > 0) o.w = w;
    if (h > 0) o.h = h;
    if (fps > 0) o.fps = fps;

    size_t elen = 1;
    for (int i = 0; i < ncuts; i++)
        elen += 512 + strlen(srcs[cuts[i].src]);
    char *edl = xmalloc(elen);
    char *vedl = xmalloc((size_t)ncuts * 24 + 1);
    char *p = edl;
    size_t left = elen;
    int first = 1;
    double at = 0.0;
    size_t vlen = 0;
    int used = 0;
    for (int i = 0; i < ncuts; i++) {
        const PCut *c = &cuts[i];
        if (c->src < 0 || c->src >= nsrcs) continue;
        double in = (double)c->local / o.fps;
        double sp = (double)c->len / o.fps;
        int n = snprintf(p, left, "%sin%.3f out%.3f at%.3f %s",
                         first ? "" : ",", in, in + sp, at, srcs[c->src]);
        if (n < 0 || (size_t)n >= left) break;
        p += n;
        left -= (size_t)n;
        int m = snprintf(vedl + vlen, (size_t)ncuts * 24 + 1 - vlen,
                         "%d + vfile\n", used + 1);
        if (m > 0) vlen += (size_t)m;
        used++;
        first = 0;
        at += sp;
    }

    if (used == 0) {
        fprintf(stderr, "partition: nothing to render\n");
        free(edl); free(vedl);
        return -1;
    }

    printf("partition: rendering %d cut(s) through the gram av engine\n", used);
    int rc = av_render(edl, vedl, NULL, NULL, NULL, out_mp4, &o);
    free(edl);
    free(vedl);
    return rc != 0 ? -1 : 0;
}