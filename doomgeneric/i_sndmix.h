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

#ifndef __I_SNDMIX__
#define __I_SNDMIX__

#include "doomtype.h"
#include <stddef.h>
#include <stdint.h>

#define SNDMIX_FRAC_BITS 16
#define SNDMIX_SHIFT     1      // accumulator >> 1 before the clamp
#define SNDMIX_MUSIC_SHIFT 6    // (m * gain) >> 6 into acc; after SNDMIX_SHIFT
                                // the net gain is gain / 128 (Q7)

typedef struct
{
    const byte *pos;        // next sample, 8-bit unsigned; NULL when idle
    const byte *end;        // one past the last sample
    unsigned int frac;      // fraction of a sample, 0..0xffff
    unsigned int step;      // input samples per output frame, 16.16
    int left;               // gain 0..254
    int right;              // gain 0..254
} sndvoice_t;

// Divides, called outside the mixing loop only.

unsigned int SNDMIX_Step(int in_rate, int out_rate);
void SNDMIX_SetGains(sndvoice_t *voice, int vol, int sep);

// Per-sample code: integer, multiplies only. SNDMIX_Mix adds to acc, which
// must be all zero before the first call (the caller zeroes it once);
// SNDMIX_Output clears each accumulator entry after reading it.

void SNDMIX_Mix(sndvoice_t *voices, int num_voices, int32_t *acc, int frames);
void SNDMIX_Output(int32_t *acc, int16_t *out, int frames);

// Add a mono signed 16-bit source to both channels of acc. gain is 0..127
// (Q7). One multiply and one shift per frame, no call. May be called before
// or after SNDMIX_Mix: the adds commute, so the output is bit-identical
// either way.

void SNDMIX_MixMono(const int16_t *src, int gain, int32_t *acc, int frames);

#endif
