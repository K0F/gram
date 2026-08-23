#ifndef GRAM_COMPOSE_H
#define GRAM_COMPOSE_H

#include "plan.h"

/*
 * Full composition pipeline (michacka's role in the merged toolkit):
 *   plan -> write EDL/vedl sidecars -> render audio passes -> optional
 *   AV video pass per movement -> concatenate -> exports.
 */

typedef struct {
    PlanCfg plan;
    int av;
    char vid_dir[1024];
} ComposeCfg;

/* resolve library dirs from env/config; dies on missing libs */
void compose_resolve_dirs(ComposeCfg *cc);

int compose_run(ComposeCfg *cc);

#endif
