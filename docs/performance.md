# Performance

Measured on an HP iPAQ h2200 (PXA255 at 398 MHz, no FPU, 64 MiB), Freedoom `-timedemo demo1`, static no-PIE builds, a quiesced board with an idle gate of at least 90 % unless a section says otherwise, interleaved runs, in frames per second.

## Headless, without a display

| build | frames per second |
|---|---|
| doomgeneric | 56.4 |
| with the texture column cache and the draw-loop locals | 67.8 |
| with a cache of 2048 entries instead of 1024 | 69.4 |

The column cache cuts data TLB walks from 7,846 to 4,159 per frame (-47 %), and a walk costs about 314 cycles on this core [meas: PMU counters]. Each value is the mean of six interleaved runs. The second run of the column-cache build in the second round (F2) was repeated and gave 68.17 and 67.41 fps; the 67.8 uses the first, and the mean would be 67.7 with the repeat. One 2048-entry run was repeated after an outlier (67.16 fps) and the repeat (69.64) is the one in the mean.

## Under picowl

Doom focused, picowl 0.3.x on the h2200. The windowed tile is 240x150 with an idle terminal in the other slot and `panel_autohide` off; in the fullscreen runs of the current build Doom is alone and `panel_autohide` is on (the portrait runs of the builds without sound did not record it). Each value is the mean of two interleaved runs [meas].

Builds without sound, the Wayland back end alone and with the column cache. The windowed column was measured with other binaries than the windowed table of the current build below, so the two are not compared with each other:

| build | windowed | portrait fullscreen | landscape fullscreen |
|---|---|---|---|
| doomgeneric Wayland back end | 27.7 | 24.4 | 19.4 |
| with the column cache | 30.4 | 26.3 | 21.3 |
| with a cache of 2048 entries | 30.7 | 26.8 | 21.6 |

The current build (column cache, sound effects, the music code without a music pack, so only effects play), fullscreen:

| options | portrait fullscreen | landscape fullscreen |
|---|---|---|
| default (thread audio) | 24.7 | 19.2 |
| `-nosound` | 26.0 | 20.9 |
| `-audio-mode tick` | 25.3 | 20.1 |
| `-audio-mode tick -present-direct -presentmode new -damage rows` | 28.2 | 22.7 |

The present options add 11.2 % (portrait) and 12.9 % (landscape) to tick mode. Tick mode with the present options is 14.3 % (portrait) and 18.6 % (landscape) faster than the default, which is the effect of both changes together. The two runs of each variant differ by at most 0.11 fps.

### Windowed, the current build

Each value is the mean of two interleaved runs in opposite orders, after one warm-up run, in the 240x150 tile with `-timedemo demo1` and an idle gate of 90 % (the board measured 94.5 to 97.9 % idle before each run) [meas]. V0 is the build with the column cache, sound effects and the music code, without a music pack. The other variants are `wl-perf-all` with the options listed; V0 is `wl-music`. The deltas are against V0.

| variant | options | run 1 | run 2 | mean | against V0 |
|---|---|---|---|---|---|
| V0 | default (thread audio) | 27.90 | 28.28 | 28.09 | |
| V7 | `-nosound` | 29.51 | 29.50 | 29.51 | +1.41 fps (+5.0 %) |
| V4 | `-audio-mode tick` | 27.23 | 28.68 | 27.96 | -0.14 fps (-0.5 %) |
| V2 | `-audio-period 1024 -audio-periods 2` | 28.52 | 28.66 | 28.59 | +0.50 fps (+1.8 %) |
| V3 | `-audio-period 2048 -audio-periods 2` | 28.54 | 28.50 | 28.52 | +0.42 fps (+1.5 %) |
| V5 | `-present-direct -presentmode new -damage rows`, thread audio | 30.48 | 30.47 | 30.47 | +2.38 fps (+8.5 %) |
| V6 | `-audio-mode tick -present-direct -presentmode new -damage rows` | 31.24 | 31.28 | 31.26 | +3.17 fps (+11.3 %) |

The two runs of V4 differ by 1.5 fps (27.23 and 28.68), while every other pair differs by at most 0.38 fps, so the mean of V4 is less certain and its small difference from V0 is not a result. V0 and the other variants are different binaries of the same source family; the difference between the binaries is not measured. In the windowed tile tick mode does not recover the cost of the audio thread, unlike in the fullscreen runs. The present options add 8.5 % with thread audio (V5) and 11.3 % with tick mode (V6); V6 is 11.8 % above V4 and 5.9 % above `-nosound`.

### Where the time goes

In the windowed base build without sound (29.9 fps, 33.5 ms per frame, in a run with counters) the game takes about 18 ms [est], picowl about 6 ms [meas] and the kernel's copy of the damaged rectangle into the display's video memory about 9 ms [meas: 7.2 MB/s over the 16-bit MediaQ bus, 60,000 bytes per commit]. About 4.8 ms of picowl's share is a fixed cost per commit that remains when almost nothing is damaged. Fullscreen portrait adds about 5 ms per frame, 3.7 ms of it copy time. The copy grows with the damaged bytes, which is what row damage reduces.

A switch between two processes costs about 450 us on this core and a switch between two threads of one process about 50 us (pipe ping-pong, 20,000 round trips, three runs, 446 to 454 us) [meas]. The cost is the change of address space, because the virtually indexed caches are cleaned on that change [inf: kernel code]. In a timedemo run under `perf` with thread audio, Doom and picowl changed address space about 5.9 times per frame (14 context switches per frame) [est: perturbed run]. The audio thread shares Doom's address space, but each time it wakes while another process runs it adds two address-space changes; tick mode removes those wakeups [inf].

### Cost of sound

Sound effects in thread mode cost 4.8 % windowed (28.09 against 29.51 fps with `-nosound`) and 5.2 % portrait and 8.2 % landscape fullscreen, two rounds each [meas]. The audio thread itself uses about 3 % of the CPU; the rest comes with its wakeups: 12,550 voluntary context switches over a fullscreen timedemo against 182 without sound, and 14.7 s of system time against 9.6 s [meas]. Fullscreen, tick mode recovers about half of the loss and stays 2.6 % (portrait) and 3.6 % (landscape) below `-nosound`. The cost of music is not measured.

### DMA for the display copy

The display driver's `present_dma` switch moves the copy to DMA. It gave no gain: 25.6 fps with DMA against 25.9 without (-1.1 %), windowed, two valid runs each, thread audio [meas]. The CPU time moves from the kernel's copy to picowl and to Doom's system time (picowl about 7,160 CPU ticks against about 4,830; Doom's system time 29.6 to 29.7 s against 15.2 to 15.3 s), because the CPU and the DMA share the bus.

The series had three rounds of each variant after a warm-up run, and a run counts only if its counters agree with its label. The third DMA run (25.91 fps) is left out because its picowl ticks (4,827) and Doom's system time (15.6 s) are those of a CPU run, and its kworker counter went negative, which is a sampling error. The second CPU run (26.68 fps) is left out because it had 3,671 voluntary context switches where every other run had 10,900 to 12,100, so it did not run the same workload. With all three runs of each, the means are 25.7 (DMA) against 26.1. The runs started with the board between 84 and 88 % idle, below the gate of the other runs.

Not measured: the h3900 and the hx4700 under a compositor, the separate gain of each present option, and the music on a device.
