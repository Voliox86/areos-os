#include "libc.h"

/* netstorm — reproduce the concurrent multi-process TCP garble (the known open
 * issue that keeps the socket demos single-process). Forks NKIDS children; each
 * opens its OWN connection to the loopback echo (127.0.0.1:7) and does ROUNDS
 * write/read rounds, checking every echo matches what it sent. A mismatch, a
 * short read, or a connect failure = the concurrent net stack corrupted a
 * session. The parent waitpid()s all kids and prints a verdict. */

#define NKIDS  4
#define ROUNDS 25

int main(void) {
    long kids[NKIDS];
    for (int k = 0; k < NKIDS; k++) {
        long pid = fork();
        if (pid == 0) {
            int bad = 0;
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) { printf("netstorm: kid %d: SOCKET failed (fd=%d)\n", k, fd); exit(1); }
            if (connect(fd, inet_ipv4(127, 0, 0, 1), 7) != 0) {
                printf("netstorm: kid %d: CONNECT failed (fd=%d)\n", k, fd);
                exit(1);
            }
            for (int r = 0; r < ROUNDS; r++) {
                char msg[32], buf[64];
                int mlen = snprintf(msg, sizeof(msg), "K%d-R%d-payload", k, r);
                write(fd, msg, mlen);
                int n = read(fd, buf, sizeof(buf) - 1);
                if (n > 0) buf[n] = '\0'; else buf[0] = '\0';
                if (n != mlen || strcmp(buf, msg) != 0) {
                    bad++;
                    printf("netstorm: kid %d round %d MISMATCH sent='%s' got='%s' (n=%d)\n",
                           k, r, msg, buf, n);
                }
            }
            close(fd);
            if (bad) printf("netstorm: kid %d FAILED with %d mismatch(es)\n", k, bad);
            exit(bad ? 1 : 0);
        }
        kids[k] = pid;
    }
    int fails = 0;
    for (int k = 0; k < NKIDS; k++) {
        int st = 0;
        waitpid((int)kids[k], &st);
        if (st != 0) fails++;
    }
    printf("netstorm: %s (%d/%d kids corrupted)\n",
           fails ? "GARBLE REPRODUCED" : "ALL CLEAN", fails, NKIDS);
    return fails ? 1 : 0;
}
