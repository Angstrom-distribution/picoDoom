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
//	The fill rule of the sound mode without an audio thread. See
//	i_alsatick.h. Integer compares and subtractions only.
//

#include "i_alsatick.h"

void TICK_Plan(const tickcfg_t *cfg, long delay, int deep, int started,
               tickplan_t *plan)
{
    unsigned int target, queued, room;

    target = deep ? cfg->buffer - cfg->period : cfg->depth;

    plan->forward = 0;
    plan->write = 0;
    plan->underrun = 0;

    if (delay < 0 || (delay == 0 && started))
    {
        // The queue ran dry; the kernel played silence for -delay frames.
        // Skip them and start again from the read pointer.

        plan->underrun = 1;
        plan->forward = delay < 0 ? (unsigned int) -delay : 0;
        queued = 0;
    }
    else
    {
        queued = (unsigned int) delay;

        if (queued > cfg->buffer)
        {
            queued = cfg->buffer;
        }
    }

    if (queued < target)
    {
        plan->write = target - queued;
    }

    room = cfg->buffer - queued;

    if (plan->write > room)
    {
        plan->write = room;
    }
}
