//
// Copyright(C) 2026 Koen Kooi
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	Pluggable music decoder interface. A decoder reads from a file image in
//	memory and produces mono signed 16-bit frames. No ALSA, no engine.
//

#ifndef __I_MUSDEC__
#define __I_MUSDEC__

#include "doomtype.h"
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    const char *name;                       // "flac"

    // Magic bytes only.

    boolean (*Probe)(const byte *data, size_t len);

    // Opens on data the caller owns: it is not copied and must outlive the
    // decoder. Sets rate and channels; NULL on error. May malloc. Game
    // thread only.

    void *(*Open)(const byte *data, size_t len,
                  unsigned int *rate, unsigned int *channels);

    // Decodes up to frames mono frames; returns the number written, 0 at
    // the end of the stream and < 0 on error (including a stream that ends
    // before its declared length). No malloc, no I/O, no float, no print.

    int (*Read)(void *dec, int16_t *out, int frames);

    // Back to frame 0. No malloc. False on error.

    boolean (*Rewind)(void *dec);

    void (*Close)(void *dec);

    // Bytes allocated by Open, for the log line.

    size_t (*Bytes)(void *dec);
} musdecoder_t;

extern const musdecoder_t *const musdecoders[];     // NULL-terminated

#endif
