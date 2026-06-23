#include "kernel.h"
#include "compositor.h"
#include "fileman_win.h"
#include "font.h"

fileman_win_t* fileman_create_ctx(void) {
    fileman_win_t* fm = (fileman_win_t*)kmalloc(sizeof(fileman_win_t));
    if (!fm) return NULL;
    memset_asm(fm, 0, sizeof(fileman_win_t));
    strncpy(fm->cwd, "/", sizeof(fm->cwd));
    fm->sel_index = -1;
    snprintf(fm->status, sizeof(fm->status), "Ready");
    return fm;
}

static void fileman_refresh(fileman_win_t* fm) {
    fm->entry_count = 0;
    fm->scroll_offset = 0;
    int fd = vfs_open(fm->cwd, 0, 0);
    if (fd < 0) {
        snprintf(fm->status, sizeof(fm->status), "Cannot open: %s", fm->cwd);
        return;
    }
    dirent_t* de = vfs_readdir(fd);
    while (de && fm->entry_count < FILEMAN_MAX_ENTRIES) {
        strncpy(fm->entries[fm->entry_count], de->name, 63);
        fm->entries[fm->entry_count][63] = '\0';
        fm->entry_types[fm->entry_count] = de->type;
        fm->entry_count++;
        de = vfs_readdir(fd);
    }
    vfs_close(fd);
    snprintf(fm->status, sizeof(fm->status), "%d entries", fm->entry_count);
}

static void fileman_cd(fileman_win_t* fm, const char* dir) {
    char newpath[256];
    if (strcmp(dir, "..") == 0) {
        if (strcmp(fm->cwd, "/") == 0) return;
        char* last_slash = NULL;
        for (int i = 0; fm->cwd[i]; i++)
            if (fm->cwd[i] == '/') last_slash = &fm->cwd[i];
        if (last_slash && last_slash > fm->cwd) {
            *last_slash = '\0';
            strncpy(newpath, fm->cwd, sizeof(newpath));
            *last_slash = '/';
        } else {
            strncpy(newpath, "/", sizeof(newpath));
        }
    } else if (dir[0] == '/') {
        strncpy(newpath, dir, sizeof(newpath));
    } else {
        if (strcmp(fm->cwd, "/") == 0)
            snprintf(newpath, sizeof(newpath), "/%s", dir);
        else
            snprintf(newpath, sizeof(newpath), "%s/%s", fm->cwd, dir);
    }
    newpath[255] = '\0';
    int fd = vfs_open(newpath, 0, 0);
    if (fd >= 0) {
        vfs_close(fd);
        strncpy(fm->cwd, newpath, sizeof(fm->cwd));
        fm->cwd[255] = '\0';
        fileman_refresh(fm);
    } else {
        snprintf(fm->status, sizeof(fm->status), "Cannot enter: %s", newpath);
    }
}

void fileman_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    fileman_win_t* fm = (fileman_win_t*)win->reserved;
    if (!fm) return;

    fb_fill_rect(cx, cy, cw, ch, fb_rgb(30,30,35));
    uint32_t char_h = FONT_HEIGHT;

    int y = cy + 4;
    fb_fill_rect(cx, y, cw, char_h + 4, fb_rgb(40,45,55));
    font_draw_string(cx + 4, y + 2, fm->cwd, fb_rgb(200,200,255), fb_rgb(40,45,55));
    y += char_h + 8;

    fb_fill_rect(cx, y, cw, char_h + 2, fb_rgb(50,55,65));
    font_draw_string(cx + 4, y + 1, "Name", fb_rgb(255,255,255), fb_rgb(50,55,65));
    y += char_h + 4;

    int max_rows = (cy + (int)ch - y - 20) / (int)char_h;
    if (max_rows < 1) max_rows = 1;

    for (int i = fm->scroll_offset; i < fm->entry_count && (i - fm->scroll_offset) < max_rows; i++) {
        int row = i - fm->scroll_offset;
        int ey = y + row * char_h;
        uint32_t bg = (i == fm->sel_index) ? fb_rgb(60,80,120) : fb_rgb(30,30,35);
        fb_fill_rect(cx + 2, ey, cw - 4, char_h, bg);

        char prefix = fm->entry_types[i] ? '/' : ' ';
        char display[68];
        snprintf(display, sizeof(display), "%c %s", prefix, fm->entries[i]);
        uint32_t fg = fm->entry_types[i] ? fb_rgb(100,200,255) : fb_rgb(220,220,220);
        font_draw_string(cx + 4, ey, display, fg, bg);
    }

    int status_y = cy + ch - char_h - 4;
    fb_fill_rect(cx, status_y, cw, char_h + 4, fb_rgb(40,45,55));
    font_draw_string(cx + 4, status_y + 2, fm->status, fb_rgb(180,180,180), fb_rgb(40,45,55));
}

void fileman_win_click(window_t* win, int mx, int my) {
    (void)mx;
    fileman_win_t* fm = (fileman_win_t*)win->reserved;
    if (!fm) return;

    int cy = win->y + TITLE_H;
    uint32_t ch = win->h;
    uint32_t char_h = FONT_HEIGHT;

    if (fm->entry_count == 0) fileman_refresh(fm);

    int list_start_y = cy + char_h + 16;
    int max_rows = ((int)ch - (int)char_h - 32) / (int)char_h;
    if (max_rows < 1) max_rows = 1;

    if (my >= list_start_y && my < list_start_y + max_rows * (int)char_h) {
        int idx = (my - list_start_y) / (int)char_h + fm->scroll_offset;
        if (idx >= 0 && idx < fm->entry_count) {
            fm->sel_index = idx;
            if (fm->entry_types[idx]) {
                fileman_cd(fm, fm->entries[idx]);
            } else {
                char path[512];
                if (strcmp(fm->cwd, "/") == 0)
                    snprintf(path, sizeof(path), "/%s", fm->entries[idx]);
                else
                    snprintf(path, sizeof(path), "%s/%s", fm->cwd, fm->entries[idx]);
                int fd = vfs_open(path, 0, 0);
                if (fd >= 0) {
                    char buf[512];
                    int n = vfs_read(fd, buf, sizeof(buf)-1);
                    vfs_close(fd);
                    if (n > 0) {
                        buf[n] = '\0';
                        snprintf(fm->status, sizeof(fm->status), "%s (%d bytes): %.200s", fm->entries[idx], n, buf);
                    }
                }
            }
        }
    }
}
