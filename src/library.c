#include "library.h"

#include "analysis.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>

static void push_path(char ***v, int *n, int *cap, char *path)
{
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 64;
        *v = xrealloc(*v, (size_t)*cap * sizeof(char *));
    }
    (*v)[(*n)++] = path;
}

static void scan_dir_rec(const char *dir, char ***v, int *n, int *cap)
{
    struct dirent **ents = NULL;
    int cnt = scandir(dir, &ents, NULL, alphasort);
    if (cnt < 0) {
        fprintf(stderr, "gram: warning: cannot read %s: %s\n", dir, strerror(errno));
        return;
    }
    for (int i = 0; i < cnt; i++) {
        const char *name = ents[i]->d_name;
        if (name[0] != '.') {
            char path[1024];
            if ((size_t)snprintf(path, sizeof(path), "%s/%s", dir, name) < sizeof(path)) {
                struct stat st;
                if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
                    scan_dir_rec(path, v, n, cap);
                } else if (has_audio_ext(name) && !strchr(name, ',') && !strchr(name, '"')) {
                    push_path(v, n, cap, xstrdup(path));
                }
            }
        }
        free(ents[i]);
    }
    free(ents);
}

void library_load(TrackList *lib, const char *dir, const char *label, int max_files)
{
    char **paths = NULL;
    int n = 0, cap = 0;
    scan_dir_rec(dir, &paths, &n, &cap);
    if (n == 0) die("no audio files found in %s (%s)", dir, label);
    int keep = n;
    if (max_files > 0 && n > max_files) {
        int *idx = xmalloc(sizeof(int) * (size_t)n);
        for (int i = 0; i < n; i++) idx[i] = i;
        shuffle_ints(idx, n);
        for (int i = 0; i < max_files; i++) {
            char *tmp = paths[i];
            paths[i] = paths[idx[i]];
            paths[idx[i]] = tmp;
        }
        free(idx);
        keep = max_files;
        for (int i = keep; i < n; i++) free(paths[i]);
        printf("  %s: found %d files, sampling %d\n", label, n, keep);
    }
    lib->v = xmalloc(sizeof(Track) * (size_t)keep);
    lib->cap = keep;
    lib->n = keep;
    for (int i = 0; i < keep; i++) {
        memset(&lib->v[i], 0, sizeof(Track));
        snprintf(lib->v[i].path, sizeof(lib->v[i].path), "%s", paths[i]);
        lib->v[i].tex[0] = '?';
        lib->v[i].key[0] = '?';
    }
    for (int i = 0; i < keep; i++) free(paths[i]);
    free(paths);
    if (keep == n) printf("  %s: %d files\n", label, lib->n);
}

/* michacka consumed tj's printf-formatted analysis text ("%.1f BPM",
 * "d=%.2f/s", "pulse=%.2f", "steady=%.2f"); emulate that rounding so role
 * classification matches the original planner exactly. */
static float round_dec(float x, int dec)
{
    float m = dec == 1 ? 10.0f : 100.0f;
    return floorf(x * m + 0.5f) / m;
}

void library_analyze(TrackList *lib, const char *label)
{
    int ok = 0;
    for (int i = 0; i < lib->n; i++) {
        Track *t = &lib->v[i];
        float bpm = 0.0f;
        char key[32] = "?";
        Texture tex;
        memset(&tex, 0, sizeof(tex));
        tex.rhythm = -1.0f;
        analyze_track(t->path, &bpm, key, sizeof(key), &tex);

        t->bpm = round_dec(bpm, 1);
        snprintf(t->key, sizeof(t->key), "%s", key);
        t->density = round_dec(tex.density, 2);
        t->pulse = round_dec(tex.pulse, 2);
        t->steady = round_dec(tex.steady, 2);
        snprintf(t->tex, sizeof(t->tex), "%s", texture_label(tex.rhythm));
        t->role = role_for(t->tex, t->density, t->pulse, t->steady);
        if (t->tex[0] != '?' && strcmp(t->tex, "n/a") != 0) ok++;

        if ((i + 1) % 12 == 0 || i + 1 == lib->n) {
            printf("\r  %s: analyzed %d/%d (cached hits skip instantly)", label, i + 1, lib->n);
            fflush(stdout);
        }
    }
    printf("\n");
    if (ok == 0)
        fprintf(stderr, "gram: warning: no analysis results for %s "
                        "(missing ffmpeg/soundstretch/keyfinder-cli/aubioonset?)\n", label);
}

int library_find(const TrackList *lib, const char *path)
{
    for (int i = 0; i < lib->n; i++)
        if (strcmp(lib->v[i].path, path) == 0) return i;
    const char *tail = path_tail(path);
    for (int i = 0; i < lib->n; i++)
        if (strcmp(path_tail(lib->v[i].path), tail) == 0) return i;
    return -1;
}

int role_for(const char *label, float density, float pulse, float steady)
{
    if (strcmp(label, "ambient") == 0) return ROLE_AMBIENT;
    if (strcmp(label, "pulse") == 0) return ROLE_PULSE;
    if (strcmp(label, "motion") == 0) return ROLE_MOTION;
    if (pulse >= 0.45f && density >= 0.9f) return ROLE_PULSE;
    if (density < 0.6f && steady < 0.5f) return ROLE_AMBIENT;
    return ROLE_MOTION;
}

int collect_roles(const TrackList *lib, int role, int *out)
{
    int c = 0;
    for (int i = 0; i < lib->n; i++)
        if (lib->v[i].role == role) out[c++] = i;
    return c;
}

double track_probe_duration(Track *t)
{
    if (t->dur != 0.0) return t->dur;
    char cmd[2200], q[2048];
    sh_quote(q, sizeof(q), t->path);
    snprintf(cmd, sizeof(cmd),
             "ffprobe -v error -show_entries format=duration -of default=nk=1:nw=1 %s", q);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1.0;
    char line[128] = "";
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        break;
    }
    int rc = pclose(fp);
    double d = atof(line);
    t->dur = (rc == 0 && d > 0.0) ? d : -1.0;
    return t->dur;
}
