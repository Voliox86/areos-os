// NyxOS init — first userspace program
#include "syscall.h"

int main(void) {
    write(1, "\n*** NyxOS Userspace Init ***\nPID: ", 33);
    {
        int pid = getpid();
        char buf[16];
        int i = 0;
        if (pid == 0) { buf[i++] = '0'; }
        else {
            int tmp = pid;
            while (tmp > 0 && i < 14) {
                buf[14 - (++i)] = '0' + (tmp % 10);
                tmp /= 10;
            }
        }
        buf[i] = '\n';
        write(1, buf + 14 - i, i + 1);
    }
    write(1, "Welcome to NyxOS userspace!\n", 28);
    write(1, "System initialized.\n", 20);

    for (;;) {
        for (volatile int i = 0; i < 1000000; i++);
    }
}
