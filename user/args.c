#include "libc.h"

/* Argument-passing test target: execve()'d with an argv, it prints what it
 * received (argc + each argv[i]) and exits with argc — so the parent's waitpid
 * status independently confirms how many arguments arrived. */
int main(int argc, char** argv) {
    printf("args.elf: argc=%d\n", argc);
    for (int i = 0; i < argc; i++)
        printf("  argv[%d] = \"%s\"\n", i, argv[i]);
    return argc;
}
