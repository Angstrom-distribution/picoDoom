# Performance

Measured on an HP iPAQ h2200 (PXA255 at 398 MHz, no FPU, 64 MiB), Freedoom `-timedemo demo1`, static no-PIE builds, a quiesced board with an idle gate of at least 90 % unless a section says otherwise, interleaved runs, in frames per second.

## Headless, without a display

| build | frames per second |
|---|---|
| doomgeneric | 56.4 |
| with the texture column cache and the draw-loop locals | 67.8 |
| with a cache of 2048 entries instead of 1024 | 69.4 |

The column cache cuts data TLB walks from 7,837 to about 4,156 per frame, and a walk costs about 314 cycles on this core [meas: PMU counters]. Each value is the mean of six interleaved runs; one 2048-entry run was repeated after an outlier (67.2 fps).

## Under picowl

Doom focused, picowl 0.3.x on the h2200. The windowed tile is 240x150 with an idle terminal in the other slot; in the fullscreen runs of the current build Doom is alone and `panel_autohide` is on (the portrait runs of the builds without sound did not record it). Each value is the mean of two interleaved runs [meas].

Builds without sound, the Wayland back end alone and with the column cache:

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

The present options add 11.2 % (portrait) and 12.9 % (landscape) to tick mode, and 14.3 % and 18.6 % to the default. The two runs of each variant differ by at most 0.11 fps. The windowed series of the current build is not finished; its results are added when both rounds are in.

### Where the time goes

In the windowed base build without sound (29.9 fps, 33.5 ms per frame, in a run with counters) the game takes about 18 ms [est], picowl about 6 ms [meas] and the kernel's copy of the damaged rectangle into the display's video memory about 9 ms [meas: 7.2 MB/s over the 16-bit MediaQ bus, 60,000 bytes per commit]. About 4.8 ms of picowl's share is a fixed cost per commit that remains when almost nothing is damaged. Fullscreen portrait adds about 5 ms per frame, 3.7 ms of it copy time. The copy grows with the damaged bytes, which is what row damage reduces.

A switch between two processes costs about 450 us on this core and a switch between two threads of one process about 50 us (pipe ping-pong, 20,000 round trips, three runs, 446 to 454 us) [meas]. The cost is the change of address space, because the virtually indexed caches are cleaned on that change [inf: kernel code]. In a timedemo run under `perf` Doom and picowl changed address space about 5.9 times per frame (14 context switches per frame) [est: perturbed run]. The audio thread shares Doom's address space, but each time it wakes while another process runs it adds two address-space changes; tick mode removes those wakeups [inf].

### Cost of sound

Sound effects in thread mode cost 7.0 % windowed (25.4 against 27.3 fps, three rounds each, in an earlier session than the tables above) and 5.2 % portrait and 8.2 % landscape fullscreen (two rounds) [meas]. The audio thread itself uses about 3 % of the CPU; the rest comes with its wakeups: 12,550 voluntary context switches over a fullscreen timedemo against 182 without sound, and 14.7 s of system time against 9.6 s [meas]. Tick mode recovers about half of the loss and stays 2.6 % (portrait) and 3.6 % (landscape) below `-nosound`. The cost of music is not measured.

### DMA for the display copy

The display driver's `present_dma` switch moves the copy to DMA. It gave no gain: 25.7 fps with DMA against 26.1 without, windowed, three rounds each, thread audio [meas]. The CPU time moves from the kernel's copy to picowl and to Doom's system time (picowl about 7,160 CPU ticks against about 4,700; Doom's system time doubled), because the CPU and the DMA share the bus. In one of the three DMA runs the counters look like a CPU run, so the result rests on two runs. The sound and DMA comparisons started with the board between 84 and 88 % idle, below the gate of the other runs.

Not measured: the windowed results of the current build (running), the h3900 and the hx4700 under a compositor, the separate gain of each present option, and the music on a device.
