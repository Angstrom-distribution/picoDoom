#!/bin/bash
# Link the headless music/sound harness (headless-music.c, pcmwav.c) with the
# objects of a finished static armel build of Makefile.wayland, in place of the
# Wayland back end and the PCM layer.
#
#   headless-build.sh BUILD_DOOMGENERIC_DIR OUTPUT [test]
#
# BUILD_DOOMGENERIC_DIR is the doomgeneric directory where `make -f
# Makefile.wayland` was run (it holds build-wayland/*.o). With "test" the
# sound module is compiled again with -DALSA_TICK_TEST, which adds the
# -audio-stalltest <ms> switch; the board binary never has it.
set -e
D=$1
OUT=$2
T=$(cd "$(dirname "$0")" && pwd)
cd "$D"
F="-O3 -fno-pie -no-pie -march=armv5te -mtune=xscale -mfloat-abi=soft -fsigned-char -fgnu89-inline -w -DNORMALUNIX -DLINUX -D_DEFAULT_SOURCE -DFEATURE_SOUND -DCMAP256 -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200"
W=$(mktemp -d)
arm-linux-gnueabi-gcc $F -I. -c "$T/headless-music.c" -o "$W/hl.o"
arm-linux-gnueabi-gcc $F -I. -c "$T/pcmwav.c" -o "$W/pcmwav.o"
O=$(ls build-wayland/*.o | grep -v -E "doomgeneric_wayland|protocol|i_alsapcm")
if [ "$3" = test ]; then
    arm-linux-gnueabi-gcc $F -DALSA_TICK_TEST -I. -c i_alsasound.c -o "$W/als.o"
    O=$(echo "$O" | grep -v i_alsasound)
    O="$O $W/als.o"
fi
arm-linux-gnueabi-gcc -static -no-pie $O "$W/hl.o" "$W/pcmwav.o" -o "$OUT" -lm -pthread
rm -rf "$W"
echo built "$OUT"
