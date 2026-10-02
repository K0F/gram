#ifndef GRAM_FLAGS_H
#define GRAM_FLAGS_H

#include <stdint.h>

/*
 * Table-driven flag parsing shared by every gram subcommand.
 *
 * One parser, two levels:
 *   - a fixed core set accepted by ALL commands (--w --h --fps --max
 *     --mute --out --seed); flags that are meaningless for a command are
 *     parsed and ignored there;
 *   - a command-specific flag table on top (either simple typed targets or
 *     callbacks for flags with optional shaped values).
 *
 * Both "--flag value" and "--flag=value" spellings work everywhere.
 * Positionals are collected in argument order for the command to interpret.
 */

typedef enum {
    FLAG_BOOL,   /* out: int* set to 1 */
    FLAG_INT,    /* out: int* */
    FLAG_FLOAT,  /* out: double* */
    FLAG_STR,    /* out: const char** */
    FLAG_CALL,   /* cb(ctx, name, val); val NULL means the flag was bare */
} FlagType;

typedef struct {
    const char *name;              /* "--foo", "-n" */
    int type;
    void *out;                     /* target for FLAG_BOOL/INT/FLOAT/STR */
    int (*cb)(void *ctx, const char *name, const char *val);
    int optional;                  /* FLAG_CALL: following value may be omitted */
} FlagSpec;

typedef struct {
    int w, h, fps;                 /* -1 = unset */
    long max;                      /* -1 = unset */
    int mute;
    int have_seed;
    uint64_t seed;
    const char *out;               /* NULL = unset */
} CoreFlags;

void core_flags_defaults(CoreFlags *c);

/* Parse argv[start..argc) against the core set plus `table`. Result lands in
 * `core` and in the command's targets/callbacks (ctx is handed to cb).
 * Positionals are collected into a freshly xmalloc'd array (*pos_out, caller
 * frees; NULL when there are none) with count *npos.
 * Returns:  0 ok, -1 parse error (message printed), -2 --help/-h requested. */
int flags_parse(int argc, char **argv, int start,
                const FlagSpec *table, int ntable, void *ctx,
                CoreFlags *core,
                char ***pos_out, int *npos);

#endif