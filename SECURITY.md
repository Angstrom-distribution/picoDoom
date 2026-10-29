# Security policy

PicoDoom reads WAD files, demo lumps, music files and command lines that come from elsewhere, in a program written in C. A crash or an overread on a malformed file is treated as a security issue.

## Input

WADs and demos are parsed by the Doom engine code, which was written for trusted files; PicoDoom does not change that. The music pack, a directory of FLAC files and an index, is parsed by PicoDoom's own code and by dr_flac. A truncated file must play as silence and must not crash the game; the decoder does not check the FLAC CRC, so a damaged file can play as noise, and a file is limited to 64 MiB. Reports of a file that crashes the game are security issues. Run the game as an ordinary user.

## Reporting a vulnerability

Please do not open a public GitHub issue for a suspected vulnerability. Use [GitHub's private vulnerability reporting](../../security/advisories/new) for this repository, or email the maintainer directly (see the GitHub profile at [github.com/koenkooi](https://github.com/koenkooi) for contact details).

Include, where possible:

- The component affected, and the version or commit
- A reproduction: the smallest file that triggers it, if you can share it
- The impact: denial of service, memory corruption, disclosure of files, and so on

## Supported versions

Only the `master` branch and the most recent tagged release are supported.

## Response

This is a single-maintainer, part-time project. There is no SLA, but security reports are prioritised over ordinary feature work.
