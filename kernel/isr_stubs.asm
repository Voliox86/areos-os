; isr_stubs.asm - ISRs and IRQs for NyxOS
; Exceptions 0-31, IRQs 0-15 mapped to INT 32-47

%macro ISR_NOERR 1
global isr%1
isr%1:
    push 0
    push %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push %1
    jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_NOERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

extern isr_handler
isr_common:
    pusha
    mov eax, [esp+32]
    push eax
    call isr_handler
    add esp, 4
    popa
    add esp, 8
    iret

%macro IRQ 2
global irq%1
irq%1:
    push 0
    push %2
    jmp irq_common
%endmacro

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

extern irq_handler
extern irq_scheduler_tick
extern saved_esp
extern next_esp
irq_common:
    pusha
    mov eax, [esp+36]
    push eax
    call irq_handler
    add esp, 4
    mov al, 0x20
    cmp byte [esp+36], 40
    jb .master_only
    out 0xA0, al
.master_only:
    out 0x20, al
    ; Save current ESP, call scheduler, then load new ESP
    mov [saved_esp], esp
    call irq_scheduler_tick
    mov esp, [next_esp]
    popa
    add esp, 8
    iret
