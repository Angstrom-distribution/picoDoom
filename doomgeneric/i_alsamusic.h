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
//	Hooks between the ALSA sound effects module (which owns the PCM and the
//	audio thread) and the music module.
//

#ifndef __I_ALSAMUSIC__
#define __I_ALSAMUSIC__

#include <stdint.h>

// Output rate of the running audio thread, 0 when sound is not running.

int I_ALSA_SoundRate(void);

// Called by the audio thread once per period, outside the voice lock.
// Writes frames mono frames and returns the Q7 gain to mix them with, or 0
// when there is no music this period. Never blocks on I/O, never prints.

int I_ALSA_MusicRender(int16_t *out, int frames);

#endif
