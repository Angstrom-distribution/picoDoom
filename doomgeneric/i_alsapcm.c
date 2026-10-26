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
//	Only <sound/asound.h> and six ioctls are used; none of them carries
//	a timespec, so the 64 bit time_t ABI does not matter.
//

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sound/asound.h>

#include "i_alsapcm.h"

#define MASK_INDEX(p, n) ((n) - SNDRV_PCM_HW_PARAM_FIRST_MASK)
#define INTERVAL_INDEX(p, n) ((n) - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL)

static void ParamInit(struct snd_pcm_hw_params *p)
{
    int i;

    memset(p, 0, sizeof(*p));

    for (i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_MASK -
                     SNDRV_PCM_HW_PARAM_FIRST_MASK; ++i)
    {
        memset(p->masks[i].bits, 0xff, sizeof(p->masks[i].bits));
    }

    for (i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL -
                     SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; ++i)
    {
        p->intervals[i].min = 0;
        p->intervals[i].max = ~0u;
    }

    p->rmask = ~0u;
    p->info = ~0u;
}

static void ParamSetMask(struct snd_pcm_hw_params *p, int n, unsigned int bit)
{
    struct snd_mask *m = &p->masks[MASK_INDEX(p, n)];

    memset(m->bits, 0, sizeof(m->bits));
    m->bits[bit / 32] = 1u << (bit % 32);
}

static void ParamSetRange(struct snd_pcm_hw_params *p, int n,
                          unsigned int min, unsigned int max)
{
    struct snd_interval *i = &p->intervals[INTERVAL_INDEX(p, n)];

    i->min = min;
    i->max = max;
    i->integer = 1;
}

static void ParamSetInt(struct snd_pcm_hw_params *p, int n, unsigned int val)
{
    ParamSetRange(p, n, val, val);
}

static unsigned int ParamGetInt(struct snd_pcm_hw_params *p, int n)
{
    return p->intervals[INTERVAL_INDEX(p, n)].min;
}

int PCM_Open(pcm_t *pcm, int card, int device)
{
    char path[64];
    int flags;

    memset(pcm, 0, sizeof(*pcm));
    pcm->fd = -1;

    snprintf(path, sizeof(path), "/dev/snd/pcmC%dD%dp", card, device);

    // Non-blocking open, so that a busy device fails at once.

    pcm->fd = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);

    if (pcm->fd < 0)
    {
        return -errno;
    }

    flags = fcntl(pcm->fd, F_GETFL);

    if (flags < 0 || fcntl(pcm->fd, F_SETFL, flags & ~O_NONBLOCK) < 0)
    {
        int err = errno;
        close(pcm->fd);
        pcm->fd = -1;
        return -err;
    }

    return 0;
}

static int TryHwParams(pcm_t *pcm, unsigned int rate,
                       unsigned int pmin, unsigned int pmax,
                       unsigned int nmin, unsigned int nmax)
{
    struct snd_pcm_hw_params p;

    ParamInit(&p);
    ParamSetMask(&p, SNDRV_PCM_HW_PARAM_ACCESS,
                 SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    ParamSetMask(&p, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S16_LE);
    ParamSetMask(&p, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
    ParamSetInt(&p, SNDRV_PCM_HW_PARAM_SAMPLE_BITS, 16);
    ParamSetInt(&p, SNDRV_PCM_HW_PARAM_FRAME_BITS, 32);
    ParamSetInt(&p, SNDRV_PCM_HW_PARAM_CHANNELS, 2);
    ParamSetInt(&p, SNDRV_PCM_HW_PARAM_RATE, rate);
    ParamSetRange(&p, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, pmin, pmax);
    ParamSetRange(&p, SNDRV_PCM_HW_PARAM_PERIODS, nmin, nmax);

    if (ioctl(pcm->fd, SNDRV_PCM_IOCTL_HW_PARAMS, &p) < 0)
    {
        int err = errno;

        // A failed attempt leaves the stream in OPEN; free before retrying.

        ioctl(pcm->fd, SNDRV_PCM_IOCTL_HW_FREE);
        return -err;
    }

    if (ParamGetInt(&p, SNDRV_PCM_HW_PARAM_RATE) != rate)
    {
        ioctl(pcm->fd, SNDRV_PCM_IOCTL_HW_FREE);
        return -EINVAL;
    }

    pcm->rate = rate;
    pcm->period_frames = ParamGetInt(&p, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
    pcm->periods = ParamGetInt(&p, SNDRV_PCM_HW_PARAM_PERIODS);
    return 0;
}

int PCM_SetParams(pcm_t *pcm, unsigned int rate, unsigned int period_frames,
                  unsigned int periods)
{
    struct snd_pcm_sw_params sw;
    unsigned long buffer, boundary;
    int err;

    err = TryHwParams(pcm, rate, period_frames, period_frames,
                      periods, periods);

    if (err == -EINVAL)
    {
        err = TryHwParams(pcm, rate, period_frames / 2, period_frames * 2,
                          2, 8);
    }

    if (err < 0)
    {
        return err;
    }

    buffer = (unsigned long) pcm->period_frames * pcm->periods;
    boundary = buffer;

    while (boundary * 2 <= (unsigned long) LONG_MAX - buffer)
    {
        boundary *= 2;
    }

    memset(&sw, 0, sizeof(sw));
    sw.tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
    sw.period_step = 1;
    sw.avail_min = pcm->period_frames;
    sw.start_threshold = pcm->period_frames;
    sw.stop_threshold = buffer;
    sw.silence_threshold = 0;
    sw.silence_size = 0;
    sw.boundary = boundary;

    if (ioctl(pcm->fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sw) < 0)
    {
        return -errno;
    }

    if (ioctl(pcm->fd, SNDRV_PCM_IOCTL_PREPARE) < 0)
    {
        return -errno;
    }

    return 0;
}

int PCM_Write(pcm_t *pcm, const int16_t *frames, unsigned int count)
{
    struct snd_xferi x;

    while (count > 0)
    {
        x.result = 0;
        x.buf = (void *) frames;
        x.frames = count;

        if (ioctl(pcm->fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x) < 0)
        {
            switch (errno)
            {
                case EINTR:
                    break;

                case EPIPE:
                    ++pcm->xruns;
                    // fall through
                case ESTRPIPE:
                    if (ioctl(pcm->fd, SNDRV_PCM_IOCTL_PREPARE) < 0)
                    {
                        return -errno;
                    }
                    break;

                default:
                    return -errno;
            }
            continue;
        }

        if (x.result <= 0)
        {
            continue;
        }

        frames += 2 * x.result;
        count -= x.result;
    }

    return 0;
}

void PCM_Drop(pcm_t *pcm)
{
    if (pcm->fd >= 0)
    {
        ioctl(pcm->fd, SNDRV_PCM_IOCTL_DROP);
    }
}

void PCM_Close(pcm_t *pcm)
{
    if (pcm->fd >= 0)
    {
        close(pcm->fd);
        pcm->fd = -1;
    }
}
