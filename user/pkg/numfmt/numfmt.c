#include "libc.h"

/* numfmt — format numbers into human-readable form with SI (k/M/G/…, base 1000) or IEC
 * (K/M/G/…, base 1024) suffixes, matching GNU `numfmt --to=si` / `--to=iec`. Reads
 * non-negative integers from the operands, or one per line from stdin when there are none.
 * Rounds toward +inf (GNU's default "from-zero" for non-negatives): a value < 10 of its
 * unit prints one decimal (1.5k, 1.0M), >= 10 prints an integer (13k, 977K), and a value
 * that rounds up to the divisor rescales to the next unit (999999 -> 1.0M). Without --to,
 * the number is reprinted as-is. Integer-only (ring-3 has no %f); overflow-safe. Uses exact
 * integer arithmetic, so it matches GNU/uutils `numfmt` for every value below 2^53 (the whole
 * practical range — up to ~9 PB); above that the reference tools lose precision to a double
 * while this stays exact. */

static const char* const U_SI[]  = { "", "k", "M", "G", "T", "P", "E" };
static const char* const U_IEC[] = { "", "K", "M", "G", "T", "P", "E" };

/* Format non-negative v with divisor `div` and unit table `units` into out[cap]. */
static void fmt_human(long v, long div, const char* const* units, char* out, int cap) {
    if (v < div) { snprintf(out, cap, "%ld", v); return; }   /* below the first unit: as-is */
    int u = 0; long scale = 1;
    while (u < 6 && v >= scale * div) { scale *= div; u++; }  /* scale = div^u, v in [scale, scale*div) */
    long q = v / scale, r = v % scale;
    long tenths = q * 10 + (r * 10 + scale - 1) / scale;      /* ceil(v*10/scale), no v*10 overflow */
    if (tenths >= 100) {                                      /* mantissa >= 10.0 -> integer form */
        long ival = q + (r ? 1 : 0);                         /* ceil(v/scale) */
        if (ival >= div && u < 6) snprintf(out, cap, "1.0%s", units[u + 1]);   /* rounded up a whole unit */
        else                      snprintf(out, cap, "%ld%s", ival, units[u]);
    } else {                                                 /* mantissa < 10 -> one decimal */
        snprintf(out, cap, "%ld.%ld%s", tenths / 10, tenths % 10, units[u]);
    }
}

/* Parse a non-negative decimal integer from s (all-digits); returns -1 on empty/non-digit. */
static long parse_num(const char* s) {
    if (!s || !*s) return -1;
    long v = 0;
    for (const char* p = s; *p; p++) {
        if (*p < '0' || *p > '9') return -1;
        v = v * 10 + (*p - '0');
    }
    return v;
}

static void emit(long v, int mode) {
    char out[48];
    if (mode == 1)      fmt_human(v, 1000, U_SI,  out, sizeof out);
    else if (mode == 2) fmt_human(v, 1024, U_IEC, out, sizeof out);
    else                snprintf(out, sizeof out, "%ld", v);   /* no --to: reprint */
    printf("%s\n", out);
}

int main(int argc, char** argv) {
    int mode = 0;          /* 0 none, 1 si, 2 iec */
    int nops = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--to=si") == 0)  { mode = 1; continue; }
        if (strcmp(argv[i], "--to=iec") == 0) { mode = 2; continue; }
        if (argv[i][0] == '-' && argv[i][1]) continue;   /* ignore other flags */
        long v = parse_num(argv[i]);
        if (v >= 0) { emit(v, mode); nops++; }
    }
    if (nops == 0) {                                     /* no operands: read stdin, one number per line */
        char line[64];
        while (fgets(line, sizeof line, stdin)) {
            int n = 0; while (line[n] && line[n] != '\n' && line[n] != '\r') n++;
            line[n] = '\0';
            long v = parse_num(line);
            if (v >= 0) emit(v, mode);
        }
    }
    return 0;
}
