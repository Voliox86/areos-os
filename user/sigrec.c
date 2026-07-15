#include "libc.h"

/* sigrec — multi-fault recovery via sigsetjmp/siglongjmp (v5.8.57). Catches the
 * SAME SIGSEGV three times in a row: each NULL dereference runs the handler,
 * which siglongjmp's back to sigsetjmp WITH THE SIGNAL MASK RESTORED — that
 * unblocks SIGSEGV (and clears its "in handler" state kernel-side), so the next
 * fault is delivered to the handler again. A plain setjmp/longjmp would leave
 * SIGSEGV blocked after the first catch, and the second fault would kill us. */

static sigjmp_buf recover;
static volatile int faults = 0;

static void handler(int sig) {
    (void)sig;
    faults++;
    printf("sigrec: caught SIGSEGV #%d, recovering\n", faults);
    siglongjmp(recover, 1);
}

int main(void) {
    signal(SIGSEGV, handler);
    sigsetjmp(recover, 1);               /* siglongjmp returns here, signal mask restored */
    if (faults < 3) {
        printf("sigrec: dereferencing NULL (attempt %d)...\n", faults + 1);
        volatile int* p = 0;
        int x = *p;                      /* #PF -> SIGSEGV -> handler -> siglongjmp back here */
        printf("sigrec: UNREACHABLE (x=%d)\n", x);
    }
    printf("sigrec: recovered from %d SIGSEGVs -- multi-recovery works!\n", faults);
    return (faults == 3) ? 0 : 1;
}
