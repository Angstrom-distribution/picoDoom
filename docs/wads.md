# Game data

PicoDoom needs a WAD: the game data of Doom or of a compatible game. The repository holds none. A WAD is given with `-iwad`, or found by its usual file name. Add-ons are loaded with `-file`. Tests use synthetic files and Freedoom.

## What runs

| data | state |
|---|---|
| Shareware `doom1.wad`, Ultimate Doom `doom.wad`, Doom II `doom2.wad`, Final Doom `tnt.wad` and `plutonia.wad`, Freedoom, Chex Quest, HACX | classic IWADs, run as in Chocolate Doom |
| The 2019 and later Doom and Doom II releases (`doom.wad`, `doom2.wad`, `tnt.wad`, `plutonia.wad` from the Nightdive re-release) | run, see below |
| SIGIL and SIGIL II as `-file` add-ons to `doom.wad` | run, see below |
| Master Levels and No Rest for the Living as `-file` add-ons to `doom2.wad` | run, see below |
| MBF21 add-ons, and id24 add-ons (for example Legacy of Rust and the deathmatch packs of the re-release) | refused with a message |

## The 2019 and later releases

These IWADs keep the classic maps, and add lumps for the Kex engine. The engine finds the game from the file name (`doom.wad`, `doom2.wad`, `tnt.wad`, `plutonia.wad`). A renamed file is identified from the title in its id24 `GAMECONF` lump, and by its levels when there is none; `-pack tnt` or `-pack plutonia` still overrides the game of a Doom II IWAD.

- The full-screen graphics (title, help, credits, intermission and finale backgrounds) and the status bar are wider than 320 columns, with the classic picture in the middle. A patch wider than the screen is centered and its outer columns are dropped. A narrower patch is drawn as before and an off-screen patch is still an error.
- The id24 lumps (`GAMECONF`, `SBARDEF`, `UMAPINFO`, `DEMOLOOP`, `SKYDEFS` and the like) are not read and do no harm.
- The `DMENUPIC` lump marks the IWAD as one of the Doom 3: BFG Edition style releases, so the existing BFG workarounds apply (the title page falls back to `INTERPIC` without `TITLEPIC`, the level names of the secret levels, a warning that demos and network games may go out of sync).
- The widescreen assets, the new menus and the id24 status bar are not used. The picture is the classic 4:3 one.

## SIGIL and SIGIL II

`-file sigil.wad` adds episode 5 and `-file sigil2.wad` adds episode 6 to `doom.wad`. Each shows up in the episode menu when the PWAD has the menu graphic and the first map, and `-warp 5 1` or `-warp 6 1` starts it directly.

- Level titles in the status bar and the automap read `E5M1` and the like. The names in the `UMAPINFO` lump are not read; the intermission shows the level-name graphics that the PWAD carries.
- The music of the episodes is the music of episode 4. The intermission uses the same background as episode 4, with no par times, and the end of an episode shows the end picture of episode 4 without text. The sky is `SKY5` or `SKY6` when the PWAD has a texture of that name, otherwise `SKY1`.
- Returning from the secret level goes to the map after the one with the secret exit.
- SIGIL II has flats between its own `F_START` and `F_END` markers. The flat range starts at the first `F_START` of the loaded files, so these flats are found, and a flat outside the range stops the game with an error instead of overwriting memory.
- Some views overflow the vanilla limits of 128 visplanes and 64 screens of openings. The limits are 256 and 256.

## Add-ons for Doom II

`-file nerve.wad` and `-file masterlevels.wad` replace `MAP01` and the following maps of `doom2.wad`. Use `-warp N`. The level order, the level names, the episode entries and the secret levels they define in `UMAPINFO` are not read: the game follows the vanilla order and names.

## Files that are refused

A file given with `-iwad` is read by its header and directory before it is loaded. The game stops with one of these messages:

| message | cause |
|---|---|
| `X is not a WAD file: it has no IWAD or PWAD header` | not a WAD |
| `X is damaged: the lump directory ... does not fit in the file` | truncated file |
| `X is not a Doom IWAD: it has no level data (E1M1 or MAP01)` | resources or a pack of other data. A PWAD gets the hint to use `-file` |
| `X cannot be the main WAD: it has levels but no PLAYPAL` | a PWAD of levels only, given as the main WAD. `COLORMAP` and `TEXTURE1` are checked the same way |
| `X needs MBF21 or id24 DEHACKED extensions ...` | a file with a DEHACKED lump of version 2021 (MBF21) or 2024 (id24), given with `-file` or `-iwad`. The engine has no DEHACKED support, and the things and states these files define do not exist in it |

Files for other games (Heretic, Hexen, Strife) keep the messages of the upstream code.

## Tests

`make -C doomgeneric/test wad` builds WAD files from `gen_wads.py` (a header, a directory and a few lumps of made-up content) and checks what is accepted and refused and the game named by a `GAMECONF` lump. It also draws synthetic patches wider than the screen with `V_DrawPatch` and `V_DrawPatchFlipped`. It needs python3 and the libpng headers (`v_video.c` includes `png.h`). The re-release IWADs and the add-ons above were also run by hand, with the licensed files of the author (not included): the title, menu, help and game screens, the built-in demos, the intermission and finale of episode 5, the secret level round trips of both episodes and every map of SIGIL and SIGIL II were loaded headless. The Freedoom frames (title, menus, help, game) and demo lengths are identical to the build before the change. The bunny scroll finale of episode 3 was changed for wide patches and not run. Nothing was run on a device.
