#include "kernel.h"
#include "login.h"
#include "auth.h"
#include "font.h"

extern volatile char kbd_buffer[256];
extern volatile int kbd_head;
extern volatile int kbd_tail;

static void draw_field(int x, int y, int w, int active, const char* text, int masked) {
    uint32_t bd = active ? fb_rgb(80,170,255) : fb_rgb(55,60,75);
    uint32_t bg = active ? fb_rgb(45,55,75) : fb_rgb(35,40,55);
    fb_fill_rect(x, y, w, 20, bg);
    fb_fill_rect(x, y, w, 1, bd);
    fb_fill_rect(x, y+19, w, 1, bd);
    fb_fill_rect(x, y, 1, 20, bd);
    fb_fill_rect(x+w-1, y, 1, 20, bd);
    if (text && text[0]) {
        if (masked) {
            char m[64];
            int len = 0;
            while (text[len]) len++;
            for (int i = 0; i < len && i < 63; i++) m[i] = '*';
            m[len] = '\0';
            font_draw_string(x+4, y+2, m, fb_rgb(200,200,220), bg);
        } else {
            font_draw_string(x+4, y+2, text, fb_rgb(200,200,220), bg);
        }
    }
}

int login_screen(void) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();

    serial_puts("[LOGIN] Drawing...\n");

    for (uint32_t y = 0; y < fh; y++) {
        uint32_t t = y * 255 / fh;
        uint8_t r = 12 + t * 14 / 255, g = 16 + t * 16 / 255, b = 26 + t * 22 / 255;
        if (r > 30) r = 30; if (g > 40) g = 40; if (b > 55) b = 55;
        fb_fill_rect(0, y, fw, 1, fb_rgb(r, g, b));
    }

    int px = (fw - 360) / 2, py = fh / 2 - 100;
    fb_fill_rect(px, py, 360, 210, fb_rgb(25,30,42));
    fb_fill_rect(px-1, py-1, 362, 1, fb_rgb(60,65,80));
    fb_fill_rect(px-1, py+210, 362, 1, fb_rgb(40,45,55));
    fb_fill_rect(px-1, py, 1, 210, fb_rgb(60,65,80));
    fb_fill_rect(px+360, py, 1, 210, fb_rgb(40,45,55));

    font_draw_string((fw-12*8)/2, py+10, "NyxOS Login", fb_rgb(180,210,240), fb_rgb(25,30,42));
    fb_fill_rect(px+20, py+30, 320, 1, fb_rgb(55,60,75));
    font_draw_string(px+20, py+45, "Username:", fb_rgb(150,170,200), fb_rgb(25,30,42));
    font_draw_string(px+20, py+98, "Password:", fb_rgb(150,170,200), fb_rgb(25,30,42));

    int ux = px+20, uy = py+65, uw = 320;
    int pfx = px+20, pfy = py+118, pfw = 320;

    draw_field(ux, uy, uw, 1, NULL, 0);
    draw_field(pfx, pfy, pfw, 0, NULL, 1);

    char user[32], pass[64];
    int user_pos = 0, pass_pos = 0, field = 0;
    user[0] = pass[0] = '\0';

    serial_puts("[LOGIN] Ready.\n");

    while (1) {
        __asm__ volatile("cli");
        char c = 0;
        if (kbd_tail != kbd_head) {
            c = kbd_buffer[kbd_tail];
            kbd_tail = (kbd_tail + 1) % 256;
        }
        __asm__ volatile("sti");

        if (c) {
            if (c == '\n' || c == '\r') {
                if (field == 0) {
                    field = 1;
                    draw_field(ux, uy, uw, 0, user, 0);
                    draw_field(pfx, pfy, pfw, 1, pass, 1);
                } else {
                    goto submit;
                }
            } else if ((c == '\b' || c == 0x7F) && (field == 0 ? user_pos : pass_pos) > 0) {
                if (field == 0) user[--user_pos] = '\0';
                else pass[--pass_pos] = '\0';
                draw_field(ux, uy, uw, field == 0, user, 0);
                draw_field(pfx, pfy, pfw, field == 1, pass, 1);
            } else if (c >= 32 && c < 127) {
                if (field == 0 && user_pos < 31) {
                    user[user_pos++] = c; user[user_pos] = '\0';
                    draw_field(ux, uy, uw, 1, user, 0);
                } else if (field == 1 && pass_pos < 63) {
                    pass[pass_pos++] = c; pass[pass_pos] = '\0';
                    draw_field(pfx, pfy, pfw, 1, pass, 1);
                }
            }
        } else {
            for (volatile int i = 0; i < 10000; i++);
        }
    }

submit:
    serial_puts("[LOGIN] Verifying...\n");
    int ok = auth_verify(user, pass);
    serial_puts(ok ? "[LOGIN] OK.\n" : "[LOGIN] FAIL.\n");
    if (!ok) {
        font_draw_string((fw-18*8)/2, py+180, "Invalid credentials", fb_rgb(220,60,60), fb_rgb(25,30,42));
        for (volatile int i = 0; i < 30000000; i++);
        return 0;
    }
    return 1;
}
