#include "libc.h"

/* srvdemo — userspace TCP SERVER sockets (v5.8.53). Demonstrates the new
 * bind()/listen()/accept() syscalls over loopback in a SINGLE process:
 *
 *   socket/bind(9000)/listen           -> a listening socket
 *   socket/connect(127.0.0.1:9000)     -> a client socket; the kernel auto-
 *                                          completes the handshake on the
 *                                          listen port, so connect() returns
 *   accept()                           -> hand out the established server side
 *   client write -> server read -> server write(echo) -> client read
 *
 * Kept to one process: a fork()ed client + parent server both busy-polling the
 * network at once still garble each other's loopback/TCP state (a known open
 * issue, separate from the mid-syscall resume-CR3 crash that IS fixed). A single
 * process polls one call at a time, so the server path is exercised cleanly. */

int main(void) {
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0)                           { printf("srvdemo: socket() failed\n"); return 1; }
    if (bind(lfd, INADDR_ANY, 9000) != 0)  { printf("srvdemo: bind() failed\n");   return 1; }
    if (listen(lfd, 4) != 0)               { printf("srvdemo: listen() failed\n"); return 1; }
    printf("srvdemo: listening on port 9000 (lfd=%d)\n", lfd);

    int c = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(c, inet_ipv4(127, 0, 0, 1), 9000) != 0) {
        printf("srvdemo: connect() failed\n"); return 1;
    }
    printf("srvdemo: client connected (cfd=%d)\n", c);

    const char* m = "ping from the client";
    write(c, m, strlen(m));                     /* queued to the server side */

    int afd = accept(lfd);                       /* retrieve the established connection */
    if (afd < 0) { printf("srvdemo: accept() failed\n"); return 1; }
    printf("srvdemo: accepted the client (afd=%d)\n", afd);

    char buf[64];
    int n = read(afd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("srvdemo: server got \"%s\" (%d bytes) -> echoing back\n", buf, n);
        write(afd, buf, n);
    }

    char rb[64];
    int rn = read(c, rb, sizeof(rb) - 1);        /* client reads the echo */
    if (rn > 0) { rb[rn] = '\0'; printf("srvdemo: client got reply \"%s\"\n", rb); }

    close(c);
    close(afd);
    close(lfd);

    int ok = (rn == (int)strlen(m));
    printf("srvdemo: %s\n", ok ? "OK -- server sockets work!" : "MISMATCH");
    return ok ? 0 : 1;
}
