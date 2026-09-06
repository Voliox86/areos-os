#ifndef UWIN_H
#define UWIN_H
/* Client-side drawing helpers for ring-3 window programs (the SYS_WIN_* ABI). A
 * window app renders into its OWN w*h XRGB (0x00RRGGBB) buffer, then win_present()s
 * it. These are the shared primitives every app needs so each one stops hand-rolling
 * raw pixel loops — the first reusable piece for moving GUI apps out of the kernel
 * into ring-3 clients. Pure (no syscalls); every write is clipped to [0,w) x [0,h),
 * so a rectangle partly or wholly off-buffer is trimmed, never written out of bounds. */
#include "libc.h"

/* Fill the whole client buffer with one colour. */
static inline void uwin_fill(unsigned int* buf, int w, int h, unsigned int color) {
    for (int i = 0, n = w * h; i < n; i++) buf[i] = color;
}

/* Fill an axis-aligned rectangle, clipped to the buffer. */
static inline void uwin_fill_rect(unsigned int* buf, int w, int h,
                                  int x, int y, int rw, int rh, unsigned int color) {
    if (rw <= 0 || rh <= 0) return;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + rw, y1 = y + rh;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
            buf[yy * w + xx] = color;
}

/* One horizontal run of `len` pixels starting at (x,y), clipped. Handy for the
 * per-row spans of a gradient background. */
static inline void uwin_hline(unsigned int* buf, int w, int h,
                              int x, int y, int len, unsigned int color) {
    if (y < 0 || y >= h || len <= 0) return;
    int x0 = x < 0 ? 0 : x, x1 = x + len;
    if (x1 > w) x1 = w;
    for (int xx = x0; xx < x1; xx++) buf[y * w + xx] = color;
}

/* One vertical run of `len` pixels starting at (x,y), clipped. */
static inline void uwin_vline(unsigned int* buf, int w, int h,
                              int x, int y, int len, unsigned int color) {
    if (x < 0 || x >= w || len <= 0) return;
    int y0 = y < 0 ? 0 : y, y1 = y + len;
    if (y1 > h) y1 = h;
    for (int yy = y0; yy < y1; yy++) buf[yy * w + x] = color;
}

/* Draw the 1px outline (frame) of a rectangle — the common window/panel/button
 * border. Built from the clipped h/v line runs, so an off-buffer frame is trimmed. */
static inline void uwin_rect_outline(unsigned int* buf, int w, int h,
                                     int x, int y, int rw, int rh, unsigned int color) {
    if (rw <= 0 || rh <= 0) return;
    uwin_hline(buf, w, h, x, y,          rw, color);   /* top    */
    uwin_hline(buf, w, h, x, y + rh - 1, rw, color);   /* bottom */
    uwin_vline(buf, w, h, x,          y, rh, color);   /* left   */
    uwin_vline(buf, w, h, x + rw - 1, y, rh, color);   /* right  */
}

/* Draw a 1px line from (x0,y0) to (x1,y1) in `color` (integer Bresenham — no FP),
 * clipped per pixel so any part off the buffer is simply skipped. For chart traces,
 * separators and diagonals a ring-3 window app draws itself. */
static inline void uwin_line(unsigned int* buf, int w, int h,
                             int x0, int y0, int x1, int y1, unsigned int color) {
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int sx = dx < 0 ? -1 : 1;
    int sy = dy < 0 ? -1 : 1;
    int err = (adx > ady ? adx : -ady) / 2;
    for (;;) {
        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h) buf[y0 * w + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        int e2 = err;
        if (e2 > -adx) { err -= ady; x0 += sx; }
        if (e2 <  ady) { err += adx; y0 += sy; }
    }
}

/* Fill a disc of radius r centred at (cx,cy) with `color`: every pixel whose distance from
 * the centre is <= r, clipped to the buffer. Integer-only (no sqrt/FP) — tests dx*dx+dy*dy
 * <= r*r over the bounding box. For status dots, indicator LEDs, and live-widget markers. */
static inline void uwin_fill_circle(unsigned int* buf, int w, int h,
                                    int cx, int cy, int r, unsigned int color) {
    if (r < 0) return;
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        int py = cy + dy;
        if (py < 0 || py >= h) continue;
        int dy2 = dy * dy;
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy2 > r2) continue;
            int px = cx + dx;
            if (px >= 0 && px < w) buf[py * w + px] = color;
        }
    }
}

/* Blit one 8x16 glyph bitmap `g` (16 bytes; row 0 = top, MSB = leftmost pixel) at
 * (x,y): only set bits are written, in `fg` (transparent background), clipped. */
static inline void uwin_glyph(unsigned int* buf, int w, int h, int x, int y,
                              const unsigned char* g, unsigned int fg) {
    for (int row = 0; row < 16; row++) {
        int py = y + row;
        if (py < 0 || py >= h) continue;
        unsigned int bits = g[row];
        for (int col = 0; col < 8; col++)
            if ((bits >> (7 - col)) & 1u) {
                int px = x + col;
                if (px >= 0 && px < w) buf[py * w + px] = fg;
            }
    }
}

/* Draw a NUL-terminated string at (x,y), fetching each glyph from the ONE kernel
 * font via font_glyph() (8px advance per char). Returns x just past the last glyph.
 * The first client-side text primitive for ring-3 window apps. */
static inline int uwin_text(unsigned int* buf, int w, int h, int x, int y,
                            const char* s, unsigned int fg) {
    unsigned char g[16];
    for (; *s; s++, x += 8)
        if (font_glyph((unsigned char)*s, g) == 0)
            uwin_glyph(buf, w, h, x, y, g, fg);
    return x;
}

/* Pixel width a string will occupy in the fixed 8px-advance kernel font (8 * its length). */
static inline int uwin_text_width(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n * 8;
}

/* Draw a string horizontally CENTERED on column cx (same font + clipping as uwin_text) —
 * for window/panel titles and centered labels. Returns x just past the last glyph. */
static inline int uwin_text_center(unsigned int* buf, int w, int h, int cx, int y,
                                   const char* s, unsigned int fg) {
    return uwin_text(buf, w, h, cx - uwin_text_width(s) / 2, y, s, fg);
}

/* Draw a string RIGHT-aligned so its last glyph ends at column xr — for right-aligned
 * values / numbers in a column. Returns x just past the last glyph (== xr). */
static inline int uwin_text_right(unsigned int* buf, int w, int h, int xr, int y,
                                  const char* s, unsigned int fg) {
    return uwin_text(buf, w, h, xr - uwin_text_width(s), y, s, fg);
}

/* --- ring-3 window input state (ergonomic wrapper over win_poll_event) -------
 * A window app usually wants the CURRENT input state per frame, not a raw event
 * stream. Zero-init a uwin_input_t, then uwin_input_pump() it once per frame and
 * read the fields. mouse_x/y + buttons + closed + win_w/win_h persist across pumps;
 * last_key, got_click and resized are per-pump transients (reset each pump). */
typedef struct {
    int mouse_x, mouse_y;    /* last cursor position (client-relative), persists */
    int buttons;             /* current button bitmask, persists */
    int last_key;            /* keycode seen this pump, else 0 */
    int got_click;           /* a click happened this pump */
    int click_x, click_y, click_btn;
    int closed;              /* the window's close box was pressed (sticky) */
    int resized;             /* a UWE_RESIZE arrived this pump (per-pump transient) */
    int win_w, win_h;        /* new client size from the last resize (0 until the first) */
} uwin_input_t;

/* Apply one event to the state — pure (no syscalls), the host-testable core. */
static inline void uwin_input_apply(uwin_input_t* st, const win_event_t* ev) {
    if (ev->kind == UWE_MOVE)       { st->mouse_x = (int)ev->a; st->mouse_y = (int)ev->b; st->buttons = (int)ev->c; }
    else if (ev->kind == UWE_CLICK) { st->mouse_x = (int)ev->a; st->mouse_y = (int)ev->b;
                                      st->click_x = (int)ev->a; st->click_y = (int)ev->b; st->click_btn = (int)ev->c; st->got_click = 1; }
    else if (ev->kind == UWE_KEY)   { st->last_key = (int)ev->a; }
    else if (ev->kind == UWE_CLOSE) { st->closed = 1; }
    /* A user drag-resize: the compositor dropped the backing and expects the next
     * present at the new size — surface it so the client can realloc + re-present. */
    else if (ev->kind == UWE_RESIZE){ st->resized = 1; st->win_w = (int)ev->a; st->win_h = (int)ev->b; }
}

/* Drain all pending events for window `id` into *st (call once per frame). Returns
 * the number processed, or -1 if the window is gone. */
static inline int uwin_input_pump(int id, uwin_input_t* st) {
    st->last_key = 0; st->got_click = 0; st->resized = 0;     /* per-pump transients */
    win_event_t ev; int n = 0, r;
    while ((r = win_poll_event(id, &ev)) == 1) { uwin_input_apply(st, &ev); n++; }
    return r < 0 ? -1 : n;
}
#endif
