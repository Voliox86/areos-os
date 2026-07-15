#include "libc.h"

/* segv — demonstrates a CATCHABLE CPU-exception signal (v5.8.49). It installs a
 * SIGSEGV handler, deliberately dereferences NULL, and RECOVERS via longjmp instead
 * of being terminated by the kernel. Without a handler this process would be killed
 * with status 139 (128 + SIGSEGV); here it catches the fault, jumps back to a safe
 * point, and exits 0 on its own terms. (One-shot: longjmp'ing out of a handler leaves
 * the signal blocked — siglongjmp / multi-recovery is future work.) */

static jmp_buf recover;
static volatile int caught_sig = 0;

static void segv_handler(int sig) {
    caught_sig = sig;
    longjmp(recover, 1);                    /* non-local jump back into main's setjmp */
}

int main(void) {
    signal(SIGSEGV, segv_handler);
    printf("segv: SIGSEGV handler installed; dereferencing NULL on purpose...\n");

    if (setjmp(recover) == 0) {
        volatile int* p = (volatile int*)0;    /* NULL */
        int x = *p;                            /* <-- #PF -> SIGSEGV -> segv_handler */
        printf("segv: UNREACHABLE (x=%d)\n", x);
    } else {
        printf("segv: caught signal %d and recovered -- back in main!\n", caught_sig);
    }

    printf("segv: exiting 0 (the kernel did NOT kill me).\n");
    return 0;
}
