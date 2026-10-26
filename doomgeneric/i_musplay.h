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
//	Music playback core: a mono ring filled from a decoder, with loop,
//	pause and end. No pthread, no ALSA, no engine calls, no printing.
//

#ifndef __I_MUSPLAY__
#define __I_MUSPLAY__

#include "doomtype.h"
#include "i_musdec.h"

#ifndef MUSPLAY_RING
#define MUSPLAY_RING   8192     // frames, power of two: 16 KB of int16
#endif
#ifndef MUSPLAY_CHUNK
#define MUSPLAY_CHUNK  1024     // frames per decoder Read call
#endif

typedef struct
{
    const musdecoder_t *dec;
    void *state;                // decoder handle; NULL means no song
    boolean looping, paused, ended;
    int errors;                 // decode errors since MUSPLAY_Start
    unsigned int rd, wr;        // free-running frame counters, fill = wr - rd
    int16_t ring[MUSPLAY_RING];
} musplay_t;

// Rewinds the decoder and empties the ring. Returns false if the rewind
// fails, and then nothing plays.

boolean MUSPLAY_Start(musplay_t *mp, const musdecoder_t *dec, void *state,
                      boolean looping);
void MUSPLAY_Stop(musplay_t *mp);

// Tops up the ring, then copies frames into out, with zeros where nothing
// is available. Returns true if any music frame was produced. With no song,
// or paused, the output is zeros and nothing is consumed or decoded.

boolean MUSPLAY_Render(musplay_t *mp, int16_t *out, int frames);

boolean MUSPLAY_IsPlaying(const musplay_t *mp);     // song && !finished

#endif
