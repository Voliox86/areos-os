#ifndef NYX_FOLD_H
#define NYX_FOLD_H
#include "types.h"

typedef void (*fold_emit_fn)(char c, void* ctx);

// Hard-wrap buf[0..len) so no output line exceeds `width` columns (clamped to >=1): a real '\n'
// passes through and resets the column; otherwise a '\n' is inserted just before the character
// that would overflow. Non-newline bytes (including tabs) each count as one column, matching the
// `fold` builtin. Each output byte is delivered via emit(c, ctx). Pure — no I/O; shared by the
// `fold` builtin and its self-test.
void fold_run(const char* buf, int len, int width, fold_emit_fn emit, void* ctx);

// Like fold_run but breaks at word boundaries (GNU `fold -s`): when a line would overflow,
// the break is placed AFTER the last blank (space/tab) at or before `width`, carrying the
// post-blank characters to the next line; a segment with no blank hard-breaks at `width`
// exactly as fold_run does. A real '\n' passes through and resets the column. Byte-column
// model (each byte = one column). Pure — no I/O; shared by the `fold -s` builtin and its KAT.
void fold_s_run(const char* buf, int len, int width, fold_emit_fn emit, void* ctx);

int fold_selftest(void);   // known-answer test of fold_run + fold_s_run

#endif // NYX_FOLD_H
