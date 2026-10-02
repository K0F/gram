#include "flags.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

void core_flags_defaults(CoreFlags *c)
{
    c->w = -1;
    c->h = -1;
    c->fps = -1;
    c->max = -1;
    c->mute = 0;
    c->have_seed = 0;
    c->seed = 0;
    c->out = NULL;
}

/* does arg carry flag `name`, either exactly or with =value attached? */
static int flag_carry(const char *arg, const char *name, const char **val)
{
    size_t n = strlen(name);
    if (strlen(arg) < n || strncmp(arg, name, n) != 0) return 0;
    const char *r = arg + n;
    if (*r == '=') { *val = r + 1; return 1; }
    if (*r == '\0') { *val = NULL; return 1; }
    return 0;
}

static int take_value(int argc, char **argv, int *i, const char *name,
                      const char *attached, const char **val)
{
    if (attached) { *val = attached; return 0; }
    if (*i + 1 < argc) { *val = argv[++(*i)]; return 0; }
    fprintf(stderr, "gram: %s requires a value\n", name);
    return -1;
}

int flags_parse(int argc, char **argv, int start,
                const FlagSpec *table, int ntable, void *ctx,
                CoreFlags *core, char ***pos_out, int *npos)
{
    char **pos = NULL;
    int n = 0, cap = 0;

    for (int i = start; i < argc; i++) {
        const char *arg = argv[i];

        if (arg[0] != '-' || arg[1] == '\0') {
            /* positional */
            if (n == cap) {
                cap = cap ? cap * 2 : 8;
                pos = xrealloc(pos, (size_t)cap * sizeof(char *));
            }
            pos[n++] = (char *)arg;
            continue;
        }
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            free(pos);
            return -2;
        }

        const char *val = NULL;
        const FlagSpec *f = NULL;
        for (int t = 0; t < ntable; t++) {
            if (flag_carry(arg, table[t].name, &val)) { f = &table[t]; break; }
        }

        if (f) {
            if (f->type == FLAG_BOOL) {
                if (val) {
                    fprintf(stderr, "gram: %s takes no value\n", f->name);
                    goto fail;
                }
                *(int *)f->out = 1;
                continue;
            }
            if (f->type == FLAG_INT) {
                const char *v;
                if (take_value(argc, argv, &i, f->name, val, &v)) goto fail;
                *(int *)f->out = atoi(v);
                continue;
            }
            if (f->type == FLAG_FLOAT) {
                const char *v;
                if (take_value(argc, argv, &i, f->name, val, &v)) goto fail;
                *(double *)f->out = atof(v);
                continue;
            }
            if (f->type == FLAG_STR) {
                const char *v;
                if (take_value(argc, argv, &i, f->name, val, &v)) goto fail;
                *(const char **)f->out = v;
                continue;
            }
            /* FLAG_CALL: optional=0 -> value required (attached or next token);
             * optional=1 -> bare flag OK, value swallowed only when the next
             * token is not itself a flag (--bpm auto, --master pop, ...). */
            if (!val) {
                if (i + 1 < argc && (f->optional ? argv[i + 1][0] != '-' : 1))
                    val = argv[++i];
            }
            if (f->cb && f->cb(ctx, f->name, val)) {
                fprintf(stderr, "gram: invalid argument for '%s'\n", f->name);
                goto fail;
            }
            continue;
        }

        /* core set, accepted by every command */
        const char *v = NULL;
        if (flag_carry(arg, "--mute", &val)) {
            if (val) {
                fprintf(stderr, "gram: --mute takes no value\n");
                goto fail;
            }
            core->mute = 1;
        } else if (flag_carry(arg, "--w", &val)) {
            if (take_value(argc, argv, &i, "--w", val, &v)) goto fail;
            core->w = atoi(v);
        } else if (flag_carry(arg, "--h", &val)) {
            if (take_value(argc, argv, &i, "--h", val, &v)) goto fail;
            core->h = atoi(v);
        } else if (flag_carry(arg, "--fps", &val)) {
            if (take_value(argc, argv, &i, "--fps", val, &v)) goto fail;
            core->fps = atoi(v);
        } else if (flag_carry(arg, "--max", &val)) {
            if (take_value(argc, argv, &i, "--max", val, &v)) goto fail;
            core->max = strtol(v, NULL, 10);
        } else if (flag_carry(arg, "--out", &val)) {
            if (take_value(argc, argv, &i, "--out", val, &v)) goto fail;
            core->out = v;
        } else if (flag_carry(arg, "--seed", &val)) {
            if (take_value(argc, argv, &i, "--seed", val, &v)) goto fail;
            core->seed = strtoull(v, NULL, 10);
            core->have_seed = 1;
        } else {
            fprintf(stderr, "gram: unknown option '%s'\n", arg);
            goto fail;
        }
    }

    *pos_out = pos;
    *npos = n;
    return 0;

fail:
    free(pos);
    *pos_out = NULL;
    *npos = 0;
    return -1;
}