# Agent guide for picoDoom

PicoDoom is Doom for small handhelds, a fork of doomgeneric. Read the README for the scope and what is decided.

## Git

Commit locally. Never run `git push`, rewrite history or tag a release without the owner's permission. Stage exact paths, one logical change per commit. `origin` is this repository and `upstream` is `ozkl/doomgeneric`; keep the differences from upstream small and in files of their own where it is possible, so that upstream changes still merge.

## Commit messages

- Subject: `component: summary`, imperative, for example `wayland: add -fullscreen`.
- Body: a sentence or two of why, in plain prose. Leave it out when the subject says it.
- End with `Signed-off-by:` read from `git config user.name` and `user.email`, never made up. Add `Assisted-by: <tool>:<model>` when an AI assisted. Never a `Co-Authored-By:` or session trailer.

## Public text

READMEs, documents, comments and commit messages are read by experienced developers who know nothing of how the work was done.

- Keep them short and factual. No marketing and no filler such as "honest", "load-bearing", "robust", "carefully", "comprehensive", "seamless" or "deliberately".
- State what is decided. Mark a plan as a plan.
- Use generic wording for the hardware: handhelds with a QVGA-class panel and about 64 MiB of RAM. Name a device only as an example or as a tested device.
- A tool name that starts a sentence takes a capital, like any other word: "PicoDoom plays ...". Inline code stays as typed.
- Notes about how an image was made go in an HTML comment, not in visible text.
- Do not refer to files, repositories or sessions the reader cannot open, or to history ("was X, now Y").
- Never write "Yocto" for the build system or ecosystem: say OpenEmbedded or bitbake. Never use Poky.

## Game data

This repository is public. Never commit, package or publish a WAD, or anything rendered from a WAD that is not freely redistributable: music packs, extracted lumps, screenshots with licensed artwork. Tests and pictures use Freedoom. A pack made from a commercial WAD stays on the machine of the person who owns the game.

## Code

- Comments say why, never what the next line does.
- Every new file carries an SPDX license identifier, or an entry in `REUSE.toml`. Check with `reuse lint`. Code taken from elsewhere must be under a license compatible with GPL-2.0-or-later.
- C in the style of the surrounding code: the Wayland back end and the files added by PicoDoom use tabs, the engine files keep their own style. The code is built with `-fgnu89-inline`.
- Nothing floating point and no divide in a loop that runs per sample or per pixel: the target cores have no FPU and a slow divide. Check the object code of a changed hot loop with `objdump`.
- Anything that wakes another process or thread costs a cache wipe on the target (virtually indexed caches). Count the wakeups that a change adds per frame.
- Options are run-time options with the old behaviour as the default, and are listed in `docs/options.md`.

## Build and test

```sh
make -C doomgeneric -f Makefile.wayland
make -C doomgeneric/test
make -C doomgeneric/test armel
```

- The tests need `freedoom1.wad` and, for `armel`, `qemu-arm` and an ARM cross compiler. A test that exits with 77 is a skip: report it as one, and say whether a result came from the host, qemu-user or a device.
- A change to the mixer or the converter keeps the bit-exact tests passing; a change that moves pixels or samples says why in the commit.
- Performance changes are judged on a device, not on a microbenchmark or under emulation: interleaved A/B runs of the timedemo on a quiesced board, headless and under the compositor, windowed and fullscreen in both orientations, with sound on.
- Documentation in `docs/` is updated in the same change as the behaviour it describes, and states current facts.
