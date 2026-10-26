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
//	Minimal ALSA PCM playback layer on the kernel ioctl interface.
//	Signed 16 bit little endian, two channels, read/write access.
//	All int returns are 0 on success and -errno on failure.
//	Nothing in here prints.
//

#ifndef __I_ALSAPCM__
#define __I_ALSAPCM__

#include <stdint.h>

typedef struct
{
    int fd;
    unsigned int rate;
    unsigned int period_frames;
    unsigned int periods;
    unsigned int xruns;
} pcm_t;

int PCM_Open(pcm_t *pcm, int card, int device);

// Asks for the exact period size and number of periods first and, only
// if the kernel refuses, for a relaxed range. The values granted are
// stored in pcm.

int PCM_SetParams(pcm_t *pcm, unsigned int rate, unsigned int period_frames,
                  unsigned int periods);

// Blocks until all frames are written. Underruns are recovered.

int PCM_Write(pcm_t *pcm, const int16_t *frames, unsigned int count);

// Safe to call while another thread is blocked in PCM_Write; that write
// then fails.

void PCM_Drop(pcm_t *pcm);
void PCM_Close(pcm_t *pcm);

#endif
