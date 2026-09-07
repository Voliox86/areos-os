#ifndef NYX_WC_H
#define NYX_WC_H
#include "types.h"

// Count lines, words, byte count, and the longest-line length of buf[0..len). A "line" ends
// at '\n'; a "word" is a maximal run of non-whitespace (space/tab/newline/CR separate). The
// longest-line length (GNU `wc -L`) expands tabs to the next 8-column stop and counts a final
// line with no trailing newline. Any out-pointer may be NULL. Pure — no I/O; shared by the
// `wc` shell builtin and its self-test.
void wc_count(const char* buf, int len, int* lines, int* words, int* chars, int* max_len);

// Streaming form of the same count, so `wc` can process a file larger than one read
// buffer without truncating it: zero a wc_state_t, feed it each chunk with wc_accum
// (state — the in-word flag and the current column — carries across chunk boundaries),
// then call wc_finish once to account a final unterminated line. wc_count is just
// init+accum(one buffer)+finish, so both share ONE definition of the rules (KAT'd).
typedef struct { int lines, words, in_word, cur, max_len, chars; } wc_state_t;
void wc_accum(wc_state_t* st, const char* buf, int len);
void wc_finish(wc_state_t* st);

int wc_selftest(void);   // known-answer test of wc_count

#endif // NYX_WC_H
