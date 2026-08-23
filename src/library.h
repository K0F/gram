#ifndef GRAM_LIBRARY_H
#define GRAM_LIBRARY_H

#include <stddef.h>

/*
 * Audio library scanning, analysis and texture-role classification
 * (ported from michacka; now calls the analysis engine directly instead
 * of shelling out to the tj binary).
 */

enum { ROLE_AMBIENT, ROLE_MOTION, ROLE_PULSE };

typedef struct {
    char path[1024];
    double dur;
    float bpm;
    char key[16];
    float density, pulse, steady;
    char tex[16];
    int role;
} Track;

typedef struct {
    Track *v;
    int n, cap;
} TrackList;

/* recursive scan of dir for audio files (sampled down to max_files) */
void library_load(TrackList *lib, const char *dir, const char *label, int max_files);

/* analyze every track in place (uses ~/.cache/tj), filling bpm/key/tex/role.
 * metric values go through the same 1-2 decimal rounding michacka applied
 * when parsing tj's text output, so role classification is identical. */
void library_analyze(TrackList *lib, const char *label);

int library_find(const TrackList *lib, const char *path);
int role_for(const char *label, float density, float pulse, float steady);
int collect_roles(const TrackList *lib, int role, int *out);

/* lazy ffprobe duration probe; caches into t->dur */
double track_probe_duration(Track *t);

#endif
