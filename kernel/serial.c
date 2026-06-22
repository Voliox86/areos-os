#include "kernel.h"

#define SERIAL_PORT 0x3F8

void init_serial(void) {
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x80);
    outb(SERIAL_PORT + 0, 0x01);
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x03);
    outb(SERIAL_PORT + 2, 0xC7);
    outb(SERIAL_PORT + 4, 0x0B);
}

void serial_putchar(char c) {
    while (!(inb(SERIAL_PORT + 5) & 0x20));
    outb(SERIAL_PORT, c);
}

void serial_puts(const char* str) {
    while (*str) {
        if (*str == '\n') serial_putchar('\r');
        serial_putchar(*str++);
    }
}

char serial_getchar(void) {
    while (!(inb(SERIAL_PORT + 5) & 0x01));
    return inb(SERIAL_PORT);
}

char serial_getchar_nonblock(void) {
    if (inb(SERIAL_PORT + 5) & 0x01) {
        return inb(SERIAL_PORT);
    }
    return 0;
}
