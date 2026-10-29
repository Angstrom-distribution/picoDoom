# Contributing to PicoDoom

## Reporting bugs and requesting features

Use [GitHub Issues](../../issues). A bug report gives the command line, the output of the game's start-up and exit lines (they print the audio and present settings and counters), the device and kernel, and the compositor. Do not attach a WAD you may not share; name it instead.

Report security problems privately, as described in [SECURITY.md](SECURITY.md).

## Submitting changes

1. Open a [pull request](../../pulls) against `master`.
2. Every new or modified file needs an [SPDX license identifier](https://spdx.dev/learn/handling-license-info/), or an entry in `REUSE.toml`. The license texts are in [`LICENSES/`](LICENSES/); check with `reuse lint`.
3. Keep commits to one logical change. The message explains why, not what the diff already shows, and ends with a `Signed-off-by:` line from `git config`.
4. A change in behaviour comes with a test that exercises it. The commit message says how it was verified: on the host, under emulation or on which device.
5. Performance is measured on a device, with interleaved A/B runs. Emulation is for correctness, not for the timing of a real core.
6. Changes that touch upstream files are kept small, so that doomgeneric changes keep merging.
7. Never include a WAD or anything rendered from a WAD you may not redistribute.
