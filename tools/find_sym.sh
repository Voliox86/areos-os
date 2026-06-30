#!/bin/bash
cd /mnt/c/Users/kzh/Desktop/Proyectos/nyx-os/kernel
nm nyx-kernel.elf | awk '{print $1, $3}' > /tmp/sym.txt
echo "Total symbols: $(wc -l < /tmp/sym.txt)"
echo "Searching for address 0x10af67..."
while read addr name; do
    dec_addr=$((16#${addr:0:16} 2>/dev/null || 0))
    target=$((0x10af67))
    if [ $dec_addr -le $target ] && [ $dec_addr -gt $((target - 0x1000)) ]; then
        printf "%s %s\n" "$addr" "$name"
    fi
done < /tmp/sym.txt
# Try to find the last symbol before 0x10af67
while read addr name; do
    if [ "$addr" != "" ]; then
        dec_addr=$((16#$addr))
        if [ $dec_addr -le $((0x10af67)) ]; then
            last_addr=$addr
            last_name=$name
        fi
    fi
done < /tmp/sym.txt
echo "Last symbol before 0x10af67: $last_addr $last_name"
