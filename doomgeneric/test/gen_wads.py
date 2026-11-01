#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Synthetic WAD files for wadinfo_test: a header, a directory and a few small
# lumps. No game data: lump contents are made up here.
#
#   gen_wads.py DIR

import struct
import sys
from pathlib import Path


def wad(path, magic, lumps):
    data = b""
    entries = []
    for name, content in lumps:
        entries.append((12 + len(data), len(content), name))
        data += content
    ofs = 12 + len(data)
    out = struct.pack("<4sii", magic, len(lumps), ofs) + data
    for pos, size, name in entries:
        out += struct.pack("<ii8s", pos, size, name.encode().ljust(8, b"\0"))
    Path(path).write_bytes(out)


def gameconf(title):
    return ('{\r\n  "type": "gameconf",\r\n  "version": "1.0.0",\r\n'
            '  "data": { "title": "%s" }\r\n}\r\n' % title).encode()


def deh(version):
    return ("Patch File for DeHackEd v3.0\r\n\r\nDoom version = %d\r\n"
            "Patch format = 6\r\n" % version).encode()


BASE = [("PLAYPAL", b"p" * 8), ("COLORMAP", b"c" * 8), ("TEXTURE1", b"t" * 8)]
MAP = [("MAP01", b""), ("THINGS", b"x" * 10)]


def main():
    d = Path(sys.argv[1])
    d.mkdir(parents=True, exist_ok=True)
    wad(d / "iwad-doom2.wad", b"IWAD", BASE + MAP)
    wad(d / "iwad-doom1.wad", b"IWAD", BASE + [("E1M1", b""), ("THINGS", b"x" * 10)])
    wad(d / "iwad-rerelease-plut.wad", b"IWAD",
        [("GAMECONF", gameconf("The Plutonia Experiment")),
         ("DMENUPIC", b"m")] + BASE + MAP)
    wad(d / "iwad-rerelease-tnt.wad", b"IWAD",
        [("GAMECONF", gameconf("TNT: Evilution"))] + BASE + MAP)
    wad(d / "iwad-rerelease-doom2.wad", b"IWAD",
        [("GAMECONF", gameconf("Doom II"))] + BASE + MAP)
    wad(d / "iwad-rerelease-doom.wad", b"IWAD",
        [("GAMECONF", gameconf("Doom"))] + BASE + [("E1M1", b"")])
    wad(d / "pwad-maps.wad", b"PWAD", MAP)
    wad(d / "pwad-addon-episode.wad", b"PWAD", [("M_EPI5", b"m"), ("E5M1", b"")])
    wad(d / "pwad-id24.wad", b"PWAD",
        [("GAMECONF", gameconf("Add-on")), ("DEHACKED", deh(2021))] + MAP)
    wad(d / "pwad-id24-2024.wad", b"PWAD", [("DEHACKED", deh(2024))] + MAP)
    wad(d / "pwad-deh21.wad", b"PWAD", [("DEHACKED", deh(21))] + MAP)
    wad(d / "pwad-resources.wad", b"PWAD", [("SBARDEF", b"{}"), ("DSFOO", b"s")])
    wad(d / "iwad-no-levels.wad", b"IWAD", BASE + [("TITLEPIC", b"x")])
    wad(d / "iwad-no-playpal.wad", b"IWAD", MAP)
    wad(d / "empty.wad", b"PWAD", [])
    (d / "not-a-wad.wad").write_bytes(b"ZWAD" + b"\0" * 64)
    (d / "short.wad").write_bytes(b"IWAD\x01\0")
    # A directory that points past the end of the file.
    (d / "bad-directory.wad").write_bytes(struct.pack("<4sii", b"IWAD", 5, 9999))


main()
