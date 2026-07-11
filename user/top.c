#include "libc.h"

/* top — a live process monitor. Redraws the process table every ~1.5 s using
 * getprocs(), staying responsive to 'q' via readkey()'s timeout (the v5.8.23
 * timed-input primitive). Each cycle clears the screen with ANSI ESC[2J/ESC[H —
 * the terminal grew minimal support for those in v5.8.23.
 *
 * NOTE: the GUI terminal window is frozen while a foreground process runs (the
 * compositor is parked in kwait), so the live refresh is observed on the serial
 * console; the window shows the final frame once top exits.
 *
 * Buffers are .bss statics: getprocs()/read() copy into them from the kernel and
 * cannot fault an unfaulted lazy-heap page (a malloc'd buffer would need a memset
 * first); .bss is resident from load. */

#define MAX_PROCS 64
static nyx_procinfo_t procs[MAX_PROCS];
static char membuf[128];

static char state_char(unsigned int st) {
    switch (st) {
        case 0: return 'P';   /* parked  */
        case 1: return 'R';   /* running */
        case 2: return 'Z';   /* zombie  */
        case 3: return 'S';   /* sleeping*/
        default: return '?';
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    int frame = 0;
    for (;;) {
        write(1, "\x1b[2J\x1b[H", 7);                 /* clear screen + cursor home */

        long n = getprocs(procs, MAX_PROCS);

        /* header line + the first line of /proc/meminfo (ties in the v5.8.22 fs) */
        long mfd = open("/proc/meminfo", O_RDONLY, 0);
        membuf[0] = '\0';
        if (mfd >= 0) {
            long r = read((int)mfd, membuf, sizeof(membuf) - 1);
            if (r < 0) r = 0;
            membuf[r] = '\0';
            close((int)mfd);
        }
        printf("NyxOS top  -  %ld processes  -  refresh #%d  -  press 'q' to quit\n", n, frame);
        if (membuf[0]) {
            printf("  ");
            for (int j = 0; membuf[j] && membuf[j] != '\n'; j++) putchar(membuf[j]);
            putchar('\n');
        }

        /* right-justified numeric columns (the user printf has no left-justify) */
        printf("\n%5s %5s %s %8s %s\n", "PID", "PPID", "S", "TIME", "CMD");
        for (long i = 0; i < n; i++)
            printf("%5u %5u %c %8u %s\n",
                   procs[i].pid, procs[i].ppid, state_char(procs[i].state),
                   procs[i].cpu_time, procs[i].comm);

        long k = readkey(1500);                       /* refresh every 1.5s or on key */
        if (k == 'q' || k == 'Q') break;
        frame++;
    }
    printf("top: bye\n");
    return 0;
}
