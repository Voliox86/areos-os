/* nyxrt.c — Nyx C runtime implementation. Freestanding, no libc calls. */
#include "nyxrt.h"

nyx_str __nyx_fmt_begin(char* buf, nyx_u64 cap) {
    (void)cap;
    if (cap) buf[0] = '\0';
    return (nyx_str){ buf, 0 };
}

/* Append n bytes, always NUL-terminating, never overflowing cap. */
static void nyx_append(nyx_str* d, char* buf, nyx_u64 cap, const char* s, nyx_u64 n) {
    nyx_u64 i = 0;
    while (i < n && d->len + 1 < cap) buf[d->len++] = s[i++];
    if (cap) buf[d->len] = '\0';
    d->ptr = buf;
}

void __nyx_fmt_str(nyx_str* d, char* buf, nyx_u64 cap, nyx_str s) {
    nyx_append(d, buf, cap, s.ptr, s.len);
}

void __nyx_fmt_i64(nyx_str* d, char* buf, nyx_u64 cap, nyx_i64 v) {
    char tmp[24];
    int  ti = 0;
    int  neg = 0;
    nyx_u64 uv;
    if (v < 0) { neg = 1; uv = (nyx_u64)(-(v + 1)) + 1; }  /* handles INT64_MIN */
    else         uv = (nyx_u64)v;
    if (uv == 0) tmp[ti++] = '0';
    while (uv) { tmp[ti++] = (char)('0' + (int)(uv % 10)); uv /= 10; }
    if (neg) tmp[ti++] = '-';
    char out[24];
    int  oi = 0;
    while (ti) out[oi++] = tmp[--ti];   /* reverse */
    nyx_append(d, buf, cap, out, (nyx_u64)oi);
}
