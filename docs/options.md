# Command line options

The options below are the ones PicoDoom adds to doomgeneric's Wayland build. The engine's own options (`-iwad`, `-timedemo`, `-playdemo`, `-warp` and so on) are unchanged. A bad value of a window or present option (`-aspect`, `-presentmode`, `-damage`, `-damage-rects`, `-keymap`) stops the game with an error message. A bad value of a sound option (`-audio-mode`, `-audio-period`, `-audio-periods`, `-audio-depth`) prints one line and the default is used.

## Window

| option | meaning |
|---|---|
| `-fullscreen` | Ask for a fullscreen window. |
| `-appid ID` | The application id, default `doomgeneric`. |
| `-title TEXT` | A fixed window title, instead of the one Doom sets from the WAD. |
| `-aspect W:H` | The shape the 320x200 picture is fitted to, default 4:3. |
| `-stretch` | Fill the whole window. |
| `-keymap CODE=NAME[,CODE=NAME...]` | Bind Linux evdev codes. NAME is `none`, a key such as `escape`, `enter`, `f5` or `x`, or an action: `up`, `down`, `left`, `right`, `strafeleft`, `straferight`, `fire`, `use`, `strafe`, `speed`, `prevweapon`, `nextweapon`, `weapon1` to `weapon8`, `map`, `menu`, `pause`. For example `-keymap 28=fire,172=menu`. |

## Present

The defaults keep the old behaviour. The settings and the damage counters are printed when the game starts and when it exits.

| option | meaning |
|---|---|
| `-present-direct` | The back end reads the engine's frame buffer itself, which saves a copy of 64,000 bytes per frame. |
| `-presentmode old\|new` | The converter from the indexed frame to the buffer. `new` is unrolled for the picture widths 200, 240 and 320 and has a generic two-pixels-per-store kernel for the others. With a 32-bit buffer `new` falls back to `old`. |
| `-damage full\|sbar\|rows` | What is converted and damaged on each commit. `full` converts every row and damages the whole picture, `sbar` compares only the status bar rows with the previous frame, `rows` compares every row the picture samples and converts and damages only the rows that changed. A commit where no row changed is skipped. |
| `-damage-rects N` | The most rectangles one commit damages, 1 to 16, default 4. |
| `-damage-noskip` | Commit even when no row changed. |

## Sound

| option | meaning |
|---|---|
| `-nosound`, `-nosfx`, `-nomusic` | Turn off all sound, the effects, or the music. |
| `-sndpcm CARD,DEVICE` | The ALSA playback device, default `0,0`. |
| `-sndrate HZ` | Use this sample rate only. By default 22050, 44100, 11025 and 48000 Hz are tried in turn and the first one the device accepts is used. |
| `-audio-mode thread\|tick` | `thread` has one audio thread with blocking writes. `tick` has no thread: the game thread keeps the device queue filled once per pass of its main loop, so a stall longer than the queue leaves a gap of silence. Default `thread`. |
| `-audio-period FRAMES` | The period size, 64 to 4096 frames at 22050 Hz, default 512 in thread mode and 1024 in tick mode. Larger periods mean fewer wakeups and a longer delay before a sound is heard. |
| `-audio-periods N` | The number of periods, 2 to 16, default 4 in thread mode and 8 in tick mode. |
| `-audio-depth MS` | In tick mode, the sound kept queued after each pass, 20 to 1000 ms, default 100. |
| `-audio-idle` | Write a block of zeros instead of mixing when no effect and no music is playing. The output does not change. |
| `-musicdir DIR` | The directory with the music pack. The default is the `DOOM_MUSIC_DIR` environment variable, then `/home/root/doom/music`. |

When the sound device cannot be opened the game prints one line and runs silent. `-nosfx` also leaves the music silent, because the music is mixed by the sound module, and music plays only when the device runs at 22050 Hz.

The game prints the audio and present settings at start (`I_ALSA_InitSound`, `DG_Init`). At exit it prints one `I_ALSA_Stats` line (mode, rate, period, periods, writes, frames, xruns, underruns, and the audio thread's CPU time and context switches) and one `DG_PresentStats` line (draws, commits, skipped commits, rows converted and damaged, bytes damaged).
