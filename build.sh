#!/bin/sh
# Build A314Mount (CLI + GUI) with amiga-gcc. Run inside WSL.
#   cd /mnt/c/projects/A314Mount && ./build.sh
# Needs bsdsocket at runtime (a314bsd or any TCP stack).
set -e
export PATH=/opt/amiga/bin:$PATH

CC=m68k-amigaos-gcc
# NB: strip at LINK time with -s. Do NOT post-process with m68k-amigaos-strip
# (GNU strip 2.39) - it corrupts hunk relocations and the binary crashes at
# startup (even one file at a time; it's layout-dependent). -s is safe.
CFLAGS="-O2 -noixemul -Wall -Wno-pointer-sign -fomit-frame-pointer -s"

mkdir -p out

echo "== A314Mount (CLI) =="
$CC $CFLAGS -o out/A314Mount    src/cli.c src/a314disk.c

echo "== A314MountGUI =="
$CC $CFLAGS -o out/A314MountGUI src/gui.c src/a314disk.c

ls -l out
echo "Done."
