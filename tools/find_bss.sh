#!/bin/bash
cd /mnt/c/Users/kzh/Desktop/Proyectos/nyx-os/kernel
nm nyx-kernel.bin | sort | awk '{print $1, $3}' > /tmp/sorted_syms.txt
awk '{val = strtonum("0x" $1); if (val >= 0x1595000 && val <= 0x15b9000) print}' /tmp/sorted_syms.txt
