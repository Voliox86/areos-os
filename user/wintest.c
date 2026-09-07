#include "libc.h"
#include "uwin.h"

/* wintest — the first ring-3 program to open a REAL desktop window through the
 * v6.4.354 window syscalls (#77). It creates a 320x200 window, fills a Nyx-purple
 * gradient with a moving white square, presents it every frame, and polls for
 * click / key / close / resize events — reporting each over serial. It exits after
 * ~5 s or when the window is closed. This is the end-to-end proof that win_create /
 * win_present / win_poll_event work from user space, composited by the desktop.
 *
 * Input is pumped at the TOP of each frame so a user drag-resize (UWE_RESIZE) is
 * adopted — reallocating the client buffer to the new size — BEFORE the frame draws
 * and presents. That is the correct pattern for a resizable ring-3 window: after a
 * resize the compositor drops the old backing and the next present must match the
 * new client size. */

int main(void) {
    int W = 320, H = 200;
    int id = win_create(W, H, "wintest");
    printf("wintest: win_create -> id %d\n", id);
    if (id < 0) { printf("wintest: win_create FAILED\n"); return 1; }

    unsigned int* buf = (unsigned int*)malloc((size_t)W * H * 4);
    if (!buf) { printf("wintest: malloc FAILED\n"); win_destroy(id); return 1; }

    int frames = 0, events = 0;
    uwin_input_t input = {0};
    for (int i = 0; i < 160; i++) {                 /* ~160 * 30 ms ~= 4.8 s */
        /* Pump input first: a pending drag-resize must be adopted before we draw, so
         * the present size always matches the compositor's current client size. */
        int r = uwin_input_pump(id, &input);
        if (r < 0) { printf("wintest: window gone\n"); goto done; }
        events += r;
        if (input.resized) {
            printf("wintest: RESIZE w=%d h=%d\n", input.win_w, input.win_h);
            W = input.win_w; H = input.win_h;
            free(buf);
            buf = (unsigned int*)malloc((size_t)W * H * 4);   /* widen before the *4: W/H are the runtime resize size (CWE-190) */
            if (!buf) { printf("wintest: realloc FAILED\n"); goto done; }
        }
        if (input.got_click) printf("wintest: CLICK x=%d y=%d btn=%d\n", input.click_x, input.click_y, input.click_btn);
        if (input.last_key)  printf("wintest: KEY code=%d\n", input.last_key);
        if (input.closed)    { printf("wintest: CLOSE requested\n"); goto done; }

        int bx = 10, by = H - 30, bw = 80, bh = 24;      /* a "Close" button hugging the bottom-left */
        if (input.got_click && uwin_point_in_rect(input.click_x, input.click_y, bx, by, bw, bh))
            printf("wintest: BUTTON clicked\n");          /* draw + hit-test share the SAME rect */

        int span = W - 40; if (span < 1) span = 1;      /* keep the marker on-window at any size */
        int sqx = (i * 3) % span;                        /* the square marches right */
        for (int y = 0; y < H; y++) {                    /* purple: R + B ramp, low G */
            unsigned int rr = 40 + (unsigned int)(y * 130 / H);
            unsigned int bb = 70 + (unsigned int)(y * 150 / H);
            uwin_hline(buf, W, H, 0, y, W, (rr << 16) | (0x18u << 8) | bb);
        }
        uwin_rounded_rect(buf, W, H, sqx, H / 2 - 20, 40, 40, 8, 0x00FFFFFF); /* white rounded marker */
        uwin_line(buf, W, H, sqx, H / 2 - 20, sqx + 39, H / 2 + 19, 0);   /* diagonal across it */
        uwin_text_center(buf, W, H, W / 2, 8, "wintest ring-3", 0x00FFFFFF); /* centered title (re-centers on resize) */
        uwin_rect_outline(buf, W, H, 0, 0, W, H, 0x00FFFFFF);             /* 1px window frame */
        uwin_fill_circle(buf, W, H, W - 12, 12, 4, 0x0000FF00);           /* green status dot (top-right) */
        uwin_button(buf, W, H, bx, by, bw, bh, 6, "Close", 0x00553333, 0x00FFFFFF);  /* rounded button + centered label */
        if (win_present(id, buf, W, H) != 0) { printf("wintest: present FAILED at frame %d\n", i); break; }
        if (i == 0) printf("wintest: first present OK\n");
        frames++;
        usleep(30000);
    }
done:
    printf("wintest: %d frames, %d events; destroying window\n", frames, events);
    win_destroy(id);
    free(buf);
    printf("wintest: done\n");
    return 0;
}
