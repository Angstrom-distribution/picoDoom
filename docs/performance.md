# Performance

Measured on an HP iPAQ h2200 (PXA255 at 398 MHz, no FPU, 64 MiB), Freedoom `-timedemo demo1`, static no-PIE builds, a quiesced board with an idle gate of at least 90 %, interleaved runs, in frames per second.

## Headless, without a display

| build | frames per second |
|---|---|
| doomgeneric | 56.4 |
| with the texture column cache and the draw-loop locals | 67.8 |
| with a cache of 2048 entries instead of 1024 | 69.4 |

The column cache cuts data TLB walks from about 7,850 to 4,150 per frame: a walk costs about 300 cycles on this core.

## Under picowl

Doom focused. The windowed tile is 240x150.

| layout | build | sound | frames per second |
|---|---|---|---|
| windowed | doomgeneric Wayland back end | none yet in these builds | 27.8 |
| windowed | with the column cache | none yet in these builds | 30.4 |
| windowed | column cache and sound effects, thread audio | `-nosound` | 27.3 |
| windowed | column cache and sound effects, thread audio | on | 25.4 |
| portrait fullscreen | column cache and sound effects, thread audio | on | 24.6 |
| portrait fullscreen | `-audio-mode tick` | on | 25.4 |
| portrait fullscreen | `-audio-mode tick -present-direct -presentmode new -damage rows` | on | 28.2 |

Most of a frame under picowl is not the game. In a windowed frame of about 33 ms, the game takes about 18 ms, picowl about 6 ms, and the kernel's copy of the damaged rectangle into the display's video memory about 9 ms, at 7.2 MB/s over a 16-bit bus. The copy grows with the damaged bytes, which is what row damage reduces. An address-space change costs about 400 us on this core, because the caches are virtually indexed and are wiped on a switch; Doom sees about six per frame, and a sound thread that wakes every 23 ms adds to them, which tick mode removes.

Not yet measured: the landscape layouts with these builds, the h3900 and the hx4700 under a compositor, the separate gain of each present option, and the music on a device.
