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
//	The fill rule of the sound mode without an audio thread (-audio-mode
//	tick): how many frames to mix and write on each call, given how many
//	are still queued. A pure function: no ALSA, no engine, no globals.
//

#ifndef __I_ALSATICK__
#define __I_ALSATICK__

typedef struct
{
    unsigned int depth;     // target queue depth D in frames
    unsigned int buffer;    // period * periods
    unsigned int period;
    unsigned int slice;     // max frames per mix call (1024)
} tickcfg_t;

typedef struct
{
    unsigned int forward;   // frames to skip (PCM_Forward) before writing
    unsigned int write;     // frames to mix and write
    int underrun;           // 1 if the queue ran dry since the last call
} tickplan_t;

// delay is the queued frame count (negative: the queue ran dry that many
// frames ago). deep asks for the queue to be filled to buffer - period
// instead of depth, before a known long stall. started is 0 until the first
// fill has been written; a queue of exactly 0 after that is an underrun.

void TICK_Plan(const tickcfg_t *cfg, long delay, int deep, int started,
               tickplan_t *plan);

// Called by the game once per loop pass and from the screen wipe. Fills the
// PCM queue in the tick mode and returns at once in every other mode; the
// weak declaration lets builds without the ALSA sound module link.

extern void I_ALSA_Service(int deep) __attribute__((weak));

#endif
