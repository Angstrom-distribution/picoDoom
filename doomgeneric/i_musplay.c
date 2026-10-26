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
//	Music playback core. See i_musplay.h.
//

#include <string.h>

#include "i_musplay.h"

#define RING_MASK (MUSPLAY_RING - 1)

boolean MUSPLAY_Start(musplay_t *mp, const musdecoder_t *dec, void *state,
                      boolean looping)
{
    mp->state = NULL;
    mp->dec = dec;
    mp->looping = looping;
    mp->paused = false;
    mp->ended = false;
    mp->errors = 0;
    mp->rd = 0;
    mp->wr = 0;

    if (dec == NULL || state == NULL || !dec->Rewind(state))
    {
        return false;
    }

    mp->state = state;

    return true;
}

void MUSPLAY_Stop(musplay_t *mp)
{
    mp->state = NULL;
    mp->paused = false;
    mp->ended = false;
    mp->rd = 0;
    mp->wr = 0;
}

// One decoder Read into the ring, contiguous. Returns false when nothing
// more can be decoded for now.

static boolean Decode(musplay_t *mp)
{
    unsigned int pos = mp->wr & RING_MASK;
    int n = MUSPLAY_RING - pos;
    int got;

    if (n > MUSPLAY_CHUNK)
    {
        n = MUSPLAY_CHUNK;
    }

    got = mp->dec->Read(mp->state, mp->ring + pos, n);

    if (got > 0)
    {
        mp->wr += got;
        return true;
    }

    if (got < 0)
    {
        ++mp->errors;
        mp->ended = true;
        return false;
    }

    // End of the stream.

    if (!mp->looping || !mp->dec->Rewind(mp->state))
    {
        mp->ended = true;
        return false;
    }

    // A rewound stream that gives nothing again is an empty file.

    got = mp->dec->Read(mp->state, mp->ring + pos, n);

    if (got <= 0)
    {
        if (got < 0)
        {
            ++mp->errors;
        }
        mp->ended = true;
        return false;
    }

    mp->wr += got;

    return true;
}

// Keep the ring nearly full, one chunk per call once this call's frames are
// in hand, so a start or a stall catches up over several periods instead of
// one long decode.

static void TopUp(musplay_t *mp, int frames)
{
    int reads = 0;

    while (!mp->ended
        && MUSPLAY_RING - (mp->wr - mp->rd) >= MUSPLAY_CHUNK
        && ((int) (mp->wr - mp->rd) < frames || reads < 1))
    {
        if (!Decode(mp))
        {
            break;
        }
        ++reads;
    }
}

boolean MUSPLAY_Render(musplay_t *mp, int16_t *out, int frames)
{
    unsigned int fill;
    int n, first;

    if (mp->state == NULL || mp->paused)
    {
        memset(out, 0, frames * sizeof(int16_t));
        return false;
    }

    TopUp(mp, frames);

    fill = mp->wr - mp->rd;
    n = fill < (unsigned int) frames ? (int) fill : frames;

    first = MUSPLAY_RING - (mp->rd & RING_MASK);
    if (first > n)
    {
        first = n;
    }

    memcpy(out, mp->ring + (mp->rd & RING_MASK), first * sizeof(int16_t));
    memcpy(out + first, mp->ring, (n - first) * sizeof(int16_t));
    memset(out + n, 0, (frames - n) * sizeof(int16_t));
    mp->rd += n;

    return n > 0;
}

boolean MUSPLAY_IsPlaying(const musplay_t *mp)
{
    return mp->state != NULL && !(mp->ended && mp->wr == mp->rd);
}
