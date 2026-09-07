#include "wc.h"

// Extracted verbatim from the `wc` builtin so the count is defined ONCE and can be unit-tested
// off the kernel stack. The character rules and the tab-stop math match GNU wc byte-for-byte:
//   - bytes  = len (raw, unaffected by content)
//   - words  = count of transitions into a non-separator run; a separator is any isspace()
//              byte, i.e. space, '\t', '\n', '\v' (0x0B), '\f' (0x0C) and '\r'.
//   - -L     = the widest line. '\n', '\r' and '\f' all end the current line and reset the
//              column; '\t' advances to the next multiple of 8; '\v' leaves the column
//              unchanged; every other byte advances it by one. A final unterminated line is
//              still measured. (Verified against GNU wc across the \v/\f/\r edge cases.)
// Fold one chunk into the running counts, carrying the in-word flag and the current
// column across chunk boundaries so a word or line split across two reads is counted
// once. This is the single definition of wc's character rules; wc_count wraps it.
void wc_accum(wc_state_t* st, const char* buf, int len) {
    if (len < 0) len = 0;
    for (int i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\n' || c == '\r' || c == '\f') { if (st->cur > st->max_len) st->max_len = st->cur; st->cur = 0; }
        else if (c == '\t') st->cur += 8 - (st->cur % 8);  // advance to the next 8-column tab stop
        else if (c == '\v') { /* vertical tab: no column change (matches GNU wc) */ }
        else                st->cur++;
        if (c == '\n') st->lines++;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r') st->in_word = 0;
        else if (!st->in_word) { st->in_word = 1; st->words++; }
    }
    st->chars += len;
}

// Account a final line with no trailing newline. Call once after the last wc_accum.
void wc_finish(wc_state_t* st) {
    if (st->cur > st->max_len) st->max_len = st->cur;
}

void wc_count(const char* buf, int len, int* lines, int* words, int* chars, int* max_len) {
    wc_state_t st = { 0, 0, 0, 0, 0, 0 };
    wc_accum(&st, buf, len);
    wc_finish(&st);
    if (lines)   *lines = st.lines;
    if (words)   *words = st.words;
    if (chars)   *chars = st.chars;
    if (max_len) *max_len = st.max_len;
}

// ---- known-answer self-test (`wc`) --------------------------------------------------------
// Returns 1 iff wc_count(s, |s|) yields exactly (l, w, c, L). |s| is taken as the C length,
// so embedded '\0' isn't exercised here (wc reads a byte range, never a C string, in practice).
static int wc_check(const char* s, int L_, int W_, int C_, int maxL_) {
    int len = 0; while (s[len]) len++;
    int l, w, c, mx;
    wc_count(s, len, &l, &w, &c, &mx);
    return l == L_ && w == W_ && c == C_ && mx == maxL_;
}

int wc_selftest(void) {
    // basic line: 11 printable chars + '\n'
    if (!wc_check("hello world\n", 1, 2, 12, 11)) return 1;
    // no trailing newline: 0 lines, the last (only) line still measured
    if (!wc_check("abc",           0, 1, 3, 3))   return 2;
    // empty input: all zero
    if (!wc_check("",              0, 0, 0, 0))   return 3;
    // tab expands to the next 8-col stop for -L: 'a'(1) '\t'(->8) 'b'(9) => max_len 9
    if (!wc_check("a\tb\n",        1, 2, 4, 9))   return 4;
    // runs of spaces separate words but count toward the line length
    if (!wc_check("  a  b  ",      0, 2, 8, 8))   return 5;
    // CR ends the line for -L (resets the column): "a b"(3) '\r' then '\n' => max_len 3, two lines
    if (!wc_check("a b\r\nc\n",    2, 3, 7, 3))   return 6;
    // several blank lines: 3 newlines, no words, longest line is 0
    if (!wc_check("\n\n\n",        3, 0, 3, 0))   return 7;
    // leading tab then text: '\t'(->8) 'x'(9) => max_len 9, one word
    if (!wc_check("\tx\n",         1, 1, 3, 9))   return 8;
    // vertical tab separates words and does NOT advance the column: 'a'(1) '\v' 'b'(2) => -L 2
    if (!wc_check("a\vb",          0, 2, 3, 2))   return 9;
    // form feed separates words and ends the line: 'a'(1) '\f'(reset) 'b'(1) => -L 1
    if (!wc_check("a\fb",          0, 2, 3, 1))   return 10;
    // mixed whitespace: a b c d e are five words; '\v'/'\f' both separate
    if (!wc_check("a b\tc\vd\fe\n", 1, 5, 10, 10)) return 11;
    return 0;
}
