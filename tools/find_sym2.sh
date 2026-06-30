#!/bin/bash
cd /mnt/c/Users/kzh/Desktop/Proyectos/nyx-os/kernel
nm nyx-kernel.bin > /tmp/syms.txt
echo "=== Symbols near 0x10af67 ==="
grep -E '10af[0-9a-f]' /tmp/syms.txt
echo ""
echo "=== Disassembly of sleep ==="
objdump -b binary -m i386:x86-64 -d --start-address=0xaf35 --stop-address=0xaf80 nyx-kernel.bin
echo ""
echo "=== Disassembly around crash ==="
objdump -b binary -m i386:x86-64 -d --start-address=0xaf50 --stop-address=0xaf70 nyx-kernel.bin
