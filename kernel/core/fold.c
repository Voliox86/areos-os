#include "fold.h"

// Extracted verbatim from the `fold` builtin so the wrap rule is defined ONCE and unit-testable
// off the kernel stack. A line exactly `width` long is NOT wrapped — the break is inserted only
// when the NEXT non-newline character would push the column past width (so the wrap point sits
// before the overflowing char), and a real newline always passes through and resets the column.
void fold_run(const char* buf, int len, int width, fold_emit_fn emit, void* ctx) {
    if (width < 1) width = 1;
    if (len < 0) len = 0;
    int col = 0;
    for (int i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\n') { emit('\n', ctx); col = 0; continue; }   // keep real line breaks
        if (col >= width) { emit('\n', ctx); col = 0; }          // hard-wrap before this char
        emit(c, ctx);
        col++;
    }
}

// Word-boundary variant (`fold -s`). Tracks the current output line as the range
// [line_start, i) of the input plus the index of the last blank in it; on overflow the
// break is placed AFTER that blank (the post-blank tail carries to the next line), or at
// `width` if the line holds no blank. Index-based, so no line buffer is needed and any
// width works. Verified byte-for-byte against GNU `fold -s` (LC_ALL=C, plain text).
void fold_s_run(const char* buf, int len, int width, fold_emit_fn emit, void* ctx) {
    if (width < 1) width = 1;
    if (len < 0) len = 0;
    int line_start = 0, last_blank = -1, col = 0, i = 0;
    while (i < len) {
        char c = buf[i];
        if (c == '\n') {                                        // real newline: flush + reset
            for (int k = line_start; k < i; k++) emit(buf[k], ctx);
            emit('\n', ctx);
            i++; line_start = i; last_blank = -1; col = 0;
            continue;
        }
        if (col >= width) {                                     // c would overflow -> break
            if (last_blank >= line_start) {                     // break AFTER the last blank
                for (int k = line_start; k <= last_blank; k++) emit(buf[k], ctx);
                emit('\n', ctx);
                line_start = last_blank + 1;
                last_blank = -1;
                col = i - line_start;                           // carried tail (blank-free, < width)
            } else {                                            // no blank: hard-break at width
                for (int k = line_start; k < i; k++) emit(buf[k], ctx);
                emit('\n', ctx);
                line_start = i;
                col = 0;
            }
            continue;                                           // re-examine c with the new column
        }
        if (c == ' ' || c == '\t') last_blank = i;
        col++;
        i++;
    }
    for (int k = line_start; k < len; k++) emit(buf[k], ctx);   // trailing line, no newline
}

// ---- known-answer self-test (`fold`) ------------------------------------------------------
typedef struct { char* out; int n; int cap; } fold_rec_t;
static void fold_rec_emit(char c, void* ctx) {
    fold_rec_t* r = (fold_rec_t*)ctx;
    if (r->n < r->cap - 1) r->out[r->n] = c;
    r->n++;
}
static int fold_check(const char* in, int width, const char* want) {
    int len = 0; while (in[len]) len++;
    char got[128]; fold_rec_t r; r.out = got; r.n = 0; r.cap = (int)sizeof got;
    fold_run(in, len, width, fold_rec_emit, &r);
    int wl = 0; while (want[wl]) wl++;
    if (r.n != wl) return 0;
    for (int i = 0; i < wl; i++) if (got[i] != want[i]) return 0;
    return 1;
}
static int fold_s_check(const char* in, int width, const char* want) {
    int len = 0; while (in[len]) len++;
    char got[128]; fold_rec_t r; r.out = got; r.n = 0; r.cap = (int)sizeof got;
    fold_s_run(in, len, width, fold_rec_emit, &r);
    int wl = 0; while (want[wl]) wl++;
    if (r.n != wl) return 0;
    for (int i = 0; i < wl; i++) if (got[i] != want[i]) return 0;
    return 1;
}
int fold_selftest(void) {
    if (!fold_check("abcd\n",     4, "abcd\n"))       return 1;  // exactly width -> no spurious wrap
    if (!fold_check("abcde",      4, "abcd\ne"))      return 2;  // one over -> wrap before the 5th char
    if (!fold_check("ab",         1, "a\nb"))         return 3;  // width 1
    if (!fold_check("ab\ncd",     4, "ab\ncd"))       return 4;  // real newlines preserved (reset the column)
    if (!fold_check("abcdefgh",   4, "abcd\nefgh"))   return 5;  // wrap continues cleanly
    if (!fold_check("abcd\nefghi",4, "abcd\nefgh\ni"))return 6;  // newline resets, then the next line wraps
    if (!fold_check("",           4, ""))             return 7;  // empty
    // fold_s_run (-s): break after the last blank, else hard-break at width (host-diffed vs GNU)
    if (!fold_s_check("the quick brown fox", 10, "the quick \nbrown fox")) return 8;  // blank ends the line
    if (!fold_s_check("aVeryLongWord ok",     5, "aVery\nLongW\nord \nok")) return 9;  // long word hard-breaks
    if (!fold_s_check("abcd efgh",            4, "abcd\n \nefgh"))          return 10; // exact width, then a lone space
    if (!fold_s_check("ab cd\nef gh ij",      5, "ab cd\nef \ngh ij"))      return 11; // real newline resets
    if (!fold_s_check("x",                    3, "x"))                      return 12; // shorter than width -> as-is
    return 0;
}
