#ifndef GRAM_OMICRON_H
#define GRAM_OMICRON_H

#include <stddef.h>

#define OMICRON_MAX_LETTERS 26
#define OMICRON_EPS 1e-4
#define OMICRON_N_OPS 4

/*
 * Operator Omicron: the Greek alphabet as numbers (a..z = 1..26), the
 * operator itself (omicron) as the free variable. Enumerates all 4^(n-1)
 * left-to-right expressions, or reverse-searches for expressions that
 * evaluate to a target value.
 *
 * In gram the enumeration is not just printed: every expression is emitted
 * through a callback as a sequence of operator steps, so planners can map
 * structure onto composition parameters.
 */

typedef struct {
    int n_ops;                       /* n_letters - 1 */
    unsigned char op[OMICRON_MAX_LETTERS]; /* op[i] applied to letter i+2 */
    double value;
} OmicronExpr;

typedef void (*OmicronEmit)(const OmicronExpr *e, void *ud);

/* apply one of the four operators */
double omicron_apply_op(int op, double x, double v);

/* single-character glyph for an operator index: + - x / */
char omicron_op_char(int op);

/* UTF-8 glyph used by the legacy text format ("+", "\u2212", "\u00d7", "\u00f7") */
const char *omicron_op_utf8(int op);

/*
 * Enumerate expressions over letters 1..n_letters.
 * If has_target, only expressions within OMICRON_EPS of target are emitted,
 * with interval pruning; at most limit matches are emitted in that mode.
 * Returns the number of emitted expressions.
 */
long long omicron_run(int n_letters, int has_target, double target,
                      long long limit, OmicronEmit emit, void *ud);

/* convenience collector; out must hold at least want entries.
 * returns number collected (may be < want if space is exhausted) */
long long omicron_collect(int n_letters, int has_target, double target,
                          long long limit, long long want, OmicronExpr *out);

/* legacy text rendering: "a + b − c × d = 10" into buf */
void omicron_format(const OmicronExpr *e, char *buf, size_t cap);

#endif
