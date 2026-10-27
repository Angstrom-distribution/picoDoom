# doomgeneric
The purpose of doomgeneric is to make porting Doom easier.
Of course Doom is already portable but with doomgeneric it is possible with just a few functions.

To try it you will need a WAD file (game data). If you don't own the game, shareware version is freely available (doom1.wad).

# porting
Create a file named doomgeneric_yourplatform.c and just implement these functions to suit your platform.
* DG_Init
* DG_DrawFrame
* DG_SleepMs
* DG_GetTicksMs
* DG_GetKey

|Functions            |Description|
|---------------------|-----------|
|DG_Init              |Initialize your platfrom (create window, framebuffer, etc...).
|DG_DrawFrame         |Frame is ready in DG_ScreenBuffer. Copy it to your platform's screen.
|DG_SleepMs           |Sleep in milliseconds.
|DG_GetTicksMs        |The ticks passed since launch in milliseconds.
|DG_GetKey            |Provide keyboard events.
|DG_SetWindowTitle    |Not required. This is for setting the window title as Doom sets this from WAD file.

### main loop
At start, call doomgeneric_Create().

In a loop, call doomgeneric_Tick().

In simplest form:
```
int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);

    while (1)
    {
        doomgeneric_Tick();
    }
    
    return 0;
}
```

# sound
Sound is much harder to implement! If you need sound, take a look at SDL port. It fully supports sound and music! Where to start? Define FEATURE_SOUND, assign DG_sound_module and DG_music_module.

# platforms
Ported platforms include Windows, X11, Wayland, SDL, emscripten. Just look at (doomgeneric_win.c, doomgeneric_xlib.c, doomgeneric_wayland.c, doomgeneric_sdl.c).
Makefiles provided for each platform.

## emscripten
You can try it directly here:
https://ozkl.github.io/doomgeneric/

emscripten port is based on SDL port, so it supports sound and music! For music, timidity backend is used.

## Wayland
Draws into wl_shm buffers and needs only libwayland-client, wayland-protocols and wayland-scanner. There is no sound.
```
cd doomgeneric
make -f Makefile.wayland
./doomgeneric -iwad doom1.wad
```

The window follows the size the compositor gives it. The 320x200 picture is fitted into it with a 4:3 shape; `-aspect W:H` changes the shape and `-stretch` fills the whole window.

`-keymap CODE=NAME[,CODE=NAME...]` binds Linux evdev codes. NAME is `none`, a key such as `escape`, `enter`, `f5` or `x`, or an action (`up`, `down`, `left`, `right`, `strafeleft`, `straferight`, `fire`, `use`, `strafe`, `speed`, `prevweapon`, `nextweapon`, `weapon1` to `weapon8`, `map`, `menu`, `pause`) that follows the engine's binding. For example `-keymap 28=fire,172=menu`.

`-fullscreen` asks for a fullscreen window, `-appid ID` sets the application id (default `doomgeneric`) and `-title TEXT` sets a fixed window title.

`-present-direct` makes the Wayland back end read the engine's frame buffer itself instead of a copy of it. `-presentmode old|new` picks the converter from the indexed frame to the buffer (`new` is an unrolled RGB565 converter; with a 32-bit buffer it falls back to `old`). `-damage full|sbar|rows` picks what is converted and damaged on each commit: `full` converts every row and damages the whole picture, `sbar` compares only the status bar rows against the previous frame, `rows` compares every row the picture samples. `-damage-rects N` (1 to 16, default 4) is the most rectangles one commit damages, and `-damage-noskip` commits even when no row changed. The defaults are the old behaviour. The settings and the damage counters are printed on start and on exit.

## Windows
![Windows](../screenshots/windows.png)

## X11 - Ubuntu
![Ubuntu](../screenshots/ubuntu.png)

## X11 - FreeBSD
![FreeBSD](../screenshots/freebsd.png)

## SDL
![SDL](../screenshots/sdl.png)

## Configuration files

The engine reads `default.cfg` and `doomgenericdoom.cfg` from the working directory at start and writes them back at exit. `default.cfg` holds the game settings, including the game keys such as `key_fire`; `doomgenericdoom.cfg` holds the menu keys such as `key_menu_confirm`, `key_menu_back` and `key_menu_left`.

Key values in these files are PC scancodes, not Doom key codes: 28 is Enter, 75 the left arrow, 27 the key `]`. A pad without a keyboard can fire and confirm with one key and go back with another:

```
key_fire 28
key_menu_confirm 28
key_menu_back 27
key_menu_left 75
```

The keys must also reach the engine, which `-keymap` does: `-keymap 28=enter,429=nextweapon` sends the centre key as Enter and a second key as the next-weapon action, which is bound to `]` by default.
