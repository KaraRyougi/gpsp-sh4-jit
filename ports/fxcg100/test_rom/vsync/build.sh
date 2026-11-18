#!/bin/sh
# Build the vsync test ROM: ports/fxcg100/test_rom/VSYNCTST.GBA
# Needs arm-none-eabi-gcc (Homebrew: arm-none-eabi-gcc) and python3.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
out=${1:-"$here/../VSYNCTST.GBA"}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

flags="-mcpu=arm7tdmi -mthumb-interwork"
arm-none-eabi-as $flags -o "$tmp/crt0.o" "$here/crt0.s"
arm-none-eabi-gcc $flags -mthumb -O2 -std=c99 -Wall -Wextra -ffreestanding \
	-fno-builtin -fno-tree-loop-distribute-patterns -c -o "$tmp/main.o" \
	"$here/main.c"
arm-none-eabi-gcc $flags -nostdlib -T "$here/gba.ld" -o "$tmp/vsync.elf" \
	"$tmp/crt0.o" "$tmp/main.o" -lgcc
arm-none-eabi-objcopy -O binary "$tmp/vsync.elf" "$tmp/vsync.bin"

# Header complement over 0xA0..0xBC, and pad to a whole KiB.
python3 -I - "$tmp/vsync.bin" "$out" <<'EOF'
import sys
rom = bytearray(open(sys.argv[1], 'rb').read())
chk = 0
for b in rom[0xA0:0xBD]:
    chk = (chk - b) & 0xFF
rom[0xBD] = (chk - 0x19) & 0xFF
rom += b'\xff' * (-len(rom) % 1024)
open(sys.argv[2], 'wb').write(rom)
print(f"{sys.argv[2]}: {len(rom)} bytes")
EOF
