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

/* library dirs come from plan_prepare (env/config, die on missing keys) */
int compose_run(ComposeCfg *cc);

#endif
