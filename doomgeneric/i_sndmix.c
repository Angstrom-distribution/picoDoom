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
//	Integer sound effects mixer. No ALSA, no zone, no globals.
//

#include "i_sndmix.h"

unsigned int SNDMIX_Step(int in_rate, int out_rate)
{
    return ((unsigned int) in_rate << SNDMIX_FRAC_BITS) / out_rate;
}

// vol is 0..127, sep is 0..254 (already clamped by i_sound.c).

void SNDMIX_SetGains(sndvoice_t *voice, int vol, int sep)
{
    int left, right;

    left = ((254 - sep) * vol) / 127;
    right = (sep * vol) / 127;

    if (left < 0)
    {
        left = 0;
    }
    if (left > 254)
    {
        left = 254;
    }
    if (right < 0)
    {
        right = 0;
    }
    if (right > 254)
    {
        right = 254;
    }

    voice->left = left;
    voice->right = right;
}

// Mix all voices into acc (interleaved L, R), voice by voice so that the
// accumulator stays in the data cache. The mixer adds to acc, which must be
// all zero on entry: the caller zeroes it once and SNDMIX_Output clears
// every entry it has read, so this function contains no call.

void SNDMIX_Mix(sndvoice_t *voices, int num_voices, int32_t *acc, int frames)
{
    sndvoice_t *voice;
    const byte *p, *end;
    int32_t *a;
    unsigned int frac, step;
    int v, i, s, gl, gr;

    for (v = 0; v < num_voices; ++v)
    {
        voice = &voices[v];

        if (voice->pos == NULL)
        {
            continue;
        }

        p = voice->pos;
        end = voice->end;
        frac = voice->frac;
        step = voice->step;
        gl = voice->left;
        gr = voice->right;
        a = acc;

        for (i = 0; i < frames; ++i)
        {
            s = *p - 128;
            a[0] += s * gl;
            a[1] += s * gr;
            a += 2;

            frac += step;
            p += frac >> SNDMIX_FRAC_BITS;
            frac &= (1 << SNDMIX_FRAC_BITS) - 1;

            if (p >= end)
            {
                p = NULL;
                break;
            }
        }

        voice->pos = p;
        voice->frac = frac;
    }
}

// Add a mono source to both channels. |src * gain| is at most
// 32768 * 127, which fits in an int32, and the arithmetic shift is a floor.

void SNDMIX_MixMono(const int16_t *src, int gain, int32_t *acc, int frames)
{
    int32_t *a = acc;
    int i, v;

    for (i = 0; i < frames; ++i)
    {
        v = (src[i] * gain) >> SNDMIX_MUSIC_SHIFT;
        a[0] += v;
        a[1] += v;
        a += 2;
    }
}

// Scale and clamp the accumulator to signed 16 bit, interleaved, and clear
// the accumulator for the next SNDMIX_Mix.

void SNDMIX_Output(int32_t *acc, int16_t *out, int frames)
{
    int i, x;

    for (i = 0; i < 2 * frames; ++i)
    {
        x = acc[i] >> SNDMIX_SHIFT;
        acc[i] = 0;

        if (x > 32767)
        {
            x = 32767;
        }
        if (x < -32768)
        {
            x = -32768;
        }

        out[i] = (int16_t) x;
    }
}
