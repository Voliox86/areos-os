# ============================================================
# kernel/Makefile - Compilación del kernel NyxOS v2.0.0
# ============================================================

TARGET = i686-elf
CC = $(TARGET)-gcc
AS = nasm
LD = $(TARGET)-ld

CFLAGS = -std=gnu99 -ffreestanding -Os -Wall -Wextra \
         -nostdlib -nostartfiles -nodefaultlibs \
         -fno-stack-protector -fno-pie -m32 \
         -ffunction-sections -fdata-sections \
         -I.

ASFLAGS = -f elf32

LDFLAGS = -T linker.ld -nostdlib -m elf_i386

OBJS = boot.o kernel.o gdt.o gdt_flush.o idt.o idt_load.o isr.o isr_stubs.o irq.o \
       memory.o heap.o process.o syscall.o vfs.o ext2.o net.o udp.o \
       ip.o arp.o ethernet.o rtl8139.o icmp.o timer.o keyboard.o screen.o \
       serial.o umoddi3.o paging.o switch.o dhcp.o

all: nyx-kernel.bin

nyx-kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

clean:
	rm -f *.o *.bin