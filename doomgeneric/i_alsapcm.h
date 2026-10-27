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
    unsigned long boundary;     // set by the SetParams calls
} pcm_t;

int PCM_Open(pcm_t *pcm, int card, int device);

// Asks for the exact period size and number of periods first and, only
// if the kernel refuses, for a relaxed range. The values granted are
// stored in pcm.

int PCM_SetParams(pcm_t *pcm, unsigned int rate, unsigned int period_frames,
                  unsigned int periods);

// The same for the non-blocking mode (no audio thread). The stream starts
// when start_frames are queued, an underrun never stops it (stop_threshold
// is the boundary) and the kernel overwrites every period that was played
// with silence, so an underrun plays zeros, never old samples.

int PCM_SetParamsTick(pcm_t *pcm, unsigned int rate,
                      unsigned int period_frames, unsigned int periods,
                      unsigned int start_frames);

// Switch the descriptor to non-blocking writes, or back.

int PCM_SetNonblock(pcm_t *pcm, int on);

// Frames queued and not yet played. After an underrun in the tick mode the
// value is negative: minus the number of frames of silence played since the
// queue ran dry.

int PCM_Delay(pcm_t *pcm, long *frames);

// Moves the write pointer forward by frames, which brings it up to the
// read pointer after an underrun (frames = -delay).

int PCM_Forward(pcm_t *pcm, unsigned long frames);

// One non-blocking write. Returns the number of frames written, which may
// be fewer than count, 0 if the buffer is full, or -errno.

int PCM_WriteSome(pcm_t *pcm, const int16_t *frames, unsigned int count);

// PREPARE after an error state; counts an xrun. The stream restarts with the
// next start_threshold worth of frames.

int PCM_Recover(pcm_t *pcm);

// Blocks until all frames are written. Underruns are recovered.

int PCM_Write(pcm_t *pcm, const int16_t *frames, unsigned int count);

// Safe to call while another thread is blocked in PCM_Write; that write
// then fails.

void PCM_Drop(pcm_t *pcm);
void PCM_Close(pcm_t *pcm);

#endif
