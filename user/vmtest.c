#include "libc.h"

/* vmtest — prove the user/kernel address boundary actually holds.
 *
 * Before this test's fix, user_v2p() walked the user PML4 with no address bound.
 * Every user PML4 mirrors kernel_pml4[511], which is the identity map of all
 * physical RAM, and KERNEL_BASE is exactly -2^39 — so KERNEL_BASE + X resolved
 * to physical X. Any syscall that wrote through an unvalidated user pointer was
 * therefore an arbitrary write into kernel memory, from an ordinary ring-3
 * program, with no privileges required.
 *
 * sigprocmask's `oldset` pointer was one such path, so it is what we aim here.
 *
 * A test that only checked "the kernel still works" would pass on a vulnerable
 * build. This one checks both directions: the attack must be REFUSED, and a
 * legitimate pointer must still be ACCEPTED. Either half alone proves nothing —
 * a kernel that rejected every pointer would pass the first and fail the second.
 */

#define KERNEL_BASE 0xFFFFFF8000000000UL

static int fail = 0;

/* No width specifier: this libc's printf does not implement one (it prints the
 * "%-46s" literally), which this very test surfaced. */
static void check(const char* what, int ok) {
    printf("vmtest: [%s] %s\n", ok ? "OK  " : "FAIL", what);
    if (!ok) fail = 1;
}

int main(void) {
    unsigned long legit = 0;

    /* Baseline: a real pointer into our own stack must work. If this fails the
     * guard is too strict and the other results below are meaningless. */
    long r = sigprocmask(0 /* SIG_BLOCK */, 0, &legit);
    check("legitimate oldset pointer accepted", r == 0);

    /* The attack. Aim oldset at the higher-half alias of low physical memory —
     * the kernel image lives at physical 0x100000. On a vulnerable kernel this
     * returns 0 and silently writes 8 bytes over kernel memory. */
    unsigned long* attack = (unsigned long*)(KERNEL_BASE + 0x100000);
    r = sigprocmask(0, 0, attack);
    check("higher-half kernel pointer REFUSED", r != 0);

    /* Same idea, aimed at the very start of physical RAM. */
    r = sigprocmask(0, 0, (unsigned long*)KERNEL_BASE);
    check("physical-zero alias REFUSED", r != 0);

    /* A non-canonical address must not translate either. */
    r = sigprocmask(0, 0, (unsigned long*)0x0000800000000000UL);
    check("first non-canonical address REFUSED", r != 0);

    /* The NULL page stays unmapped for user space. */
    r = sigprocmask(0, 0, (unsigned long*)0x10);
    check("null-page pointer REFUSED", r != 0);

    printf("vmtest: %s\n", fail ? "BOUNDARY BROKEN" : "USER/KERNEL BOUNDARY HOLDS");
    return fail;
}
