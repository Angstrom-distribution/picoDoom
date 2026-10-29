# Building and testing

## Wayland build

PicoDoom needs libwayland-client, wayland-protocols and wayland-scanner.

```sh
cd doomgeneric
make -f Makefile.wayland
./doomgeneric -iwad freedoom1.wad
```

On an ARM host add `CFLAGS="-O2 -fsigned-char"`: the engine assumes a signed `char`.

For a handheld the build is static and not position independent, with the flags the benchmarks used:

```sh
make -f Makefile.wayland STATIC=1 CC=arm-linux-gnueabi-gcc \
  CFLAGS="-O3 -fno-pie -no-pie -march=armv5te -mtune=xscale -mfloat-abi=soft -fsigned-char -fgnu89-inline -ffunction-sections -fdata-sections" \
  LDFLAGS="-static -no-pie -Wl,--gc-sections"
```

Debian ships no static libwayland-client, so a static build needs libwayland built for the target first. A build without position independent code is 13 to 17 % faster than the default build of a distribution on an XScale core [meas: headless timedemo, two pairs].

## Tests

The host tests are in `doomgeneric/test/`.

```sh
make -C doomgeneric/test                 # mixer, tick fill, sound module, music; native
make -C doomgeneric/test armel           # the same, static ARMv5 under qemu-arm-static
make -C doomgeneric/test present         # frame converter and damage model, native
make -C doomgeneric/test present-armel   # the same under qemu-arm-static
```

The tests need python3 and flac, and for `armel` qemu-arm-static and arm-linux-gnueabi-gcc.

They check:

- the mixer against a 64-bit reference, bit for bit, including clamping, voice ends, rate steps and the music source;
- the module on a fake PCM device, in thread and tick mode, with stalls;
- the new frame converter against the old one on the frames of the Freedoom demo, and the damage tracker against a model of the compositor;
- the music ring and the FLAC decoder, including a truncated file.

The tests that need a WAD read `freedoom1.wad` from the directory of make, or the file given with `make WAD=PATH`. Timing from emulation says nothing about a real core: speed is judged on a device.
