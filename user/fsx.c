#include "libc.h"

/* fsx — exercise the v5.8.82 filesystem syscalls: dup, rename, and the per-process
 * cwd. A tool spawned from the shell now inherits the terminal's cwd; chdir/getcwd
 * and relative paths resolve against that per-process cwd. */

int main(void) {
    char buf[128];
    struct stat st;

    /* 1) getcwd — where did we start? A tool run after `cd /somewhere` now begins
     *    there rather than at "/". */
    if (getcwd(buf, sizeof(buf)) < 0) { printf("fsx: getcwd failed\n"); return 1; }
    printf("fsx: start cwd = %s\n", buf);

    /* 2) rename — create /tmp/fsx_a, then rename it to /tmp/fsx_b. */
    mkdir("/tmp", 0);
    int fd = open("/tmp/fsx_a", O_CREAT | O_TRUNC, 0);
    if (fd < 0) { printf("fsx: open fsx_a failed\n"); return 1; }
    write(fd, "hello fsx\n", 10);
    close(fd);
    int rr = rename("/tmp/fsx_a", "/tmp/fsx_b");
    printf("fsx: rename a->b = %d ; old exists=%d new exists=%d\n",
           rr, stat("/tmp/fsx_a", &st) == 0, stat("/tmp/fsx_b", &st) == 0);

    /* 3) dup — the dup'd fd is an independent handle: it still works after the
     *    original fd is closed. */
    fd = open("/tmp/fsx_b", O_RDONLY, 0);
    int fd2 = dup(fd);
    close(fd);
    int n = read(fd2, buf, sizeof(buf) - 1);
    if (n < 0) n = 0;
    buf[n] = '\0';
    printf("fsx: dup(orig=%d)=%d ; after close(orig) the dup'd fd read %d bytes: %s",
           fd, fd2, n, buf);
    close(fd2);

    /* 4) per-process cwd — chdir into /tmp, prove a RELATIVE path resolves against
     *    it, then `..` walks back up. */
    if (chdir("/tmp") == 0) {
        getcwd(buf, sizeof(buf));
        int rel = stat("fsx_b", &st);              /* relative -> /tmp/fsx_b */
        printf("fsx: chdir(/tmp) -> cwd=%s ; relative stat(\"fsx_b\") = %d\n", buf, rel);
        chdir("..");
        getcwd(buf, sizeof(buf));
        printf("fsx: chdir(\"..\") -> cwd=%s\n", buf);
    }

    unlink("/tmp/fsx_b");
    printf("fsx: done.\n");
    return 0;
}
