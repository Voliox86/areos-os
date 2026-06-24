; Minimal CRT0 for NyxOS user programs
; Calls main(argc, argv) then exit()

global _start
extern main

section .text
_start:
    push 0      ; argv
    push 0      ; argc
    call main
    add esp, 8

    ; exit(eax)
    mov ebx, eax
    mov eax, 0  ; SYS_EXIT
    int 0x80
    hlt
