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
//	FLAC decoder plug-in on dr_flac, integer only (s16 output).
//

#include <stdlib.h>
#include <string.h>

// No asserts in the decode path: they cost a compare per call on the
// target and abort the game on a bad file, which the decoder reports anyway.

#define DRFLAC_ASSERT(expression) ((void) 0)

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_CRC
#define DR_FLAC_NO_SIMD
#include "dr_flac.h"

#include "i_musdec.h"

typedef struct
{
    drflac *flac;
    drflac_uint64 pos;          // frames decoded since the start
    drflac_uint64 total;        // frames declared by STREAMINFO, 0 if unknown
    size_t bytes;               // allocated by dr_flac
} flacdec_t;

static void *FlacMalloc(size_t sz, void *user)
{
    *(size_t *) user += sz;
    return malloc(sz);
}

static void *FlacRealloc(void *p, size_t sz, void *user)
{
    *(size_t *) user += sz;
    return realloc(p, sz);
}

static void FlacFree(void *p, void *user)
{
    (void) user;
    free(p);
}

static boolean FlacProbe(const byte *data, size_t len)
{
    return len >= 4 && memcmp(data, "fLaC", 4) == 0;
}

static void *FlacOpen(const byte *data, size_t len,
                      unsigned int *rate, unsigned int *channels)
{
    flacdec_t *d = calloc(1, sizeof(flacdec_t));
    drflac_allocation_callbacks cb;

    if (d == NULL)
    {
        return NULL;
    }

    cb.pUserData = &d->bytes;
    cb.onMalloc = FlacMalloc;
    cb.onRealloc = FlacRealloc;
    cb.onFree = FlacFree;

    d->flac = drflac_open_memory(data, len, &cb);

    if (d->flac == NULL)
    {
        free(d);
        return NULL;
    }

    d->total = d->flac->totalPCMFrameCount;
    *rate = d->flac->sampleRate;
    *channels = d->flac->channels;

    return d;
}

static int FlacRead(void *dec, int16_t *out, int frames)
{
    flacdec_t *d = dec;
    drflac_uint64 got;

    got = drflac_read_pcm_frames_s16(d->flac, frames, out);
    d->pos += got;

    if (got == 0 && d->total != 0 && d->pos != d->total)
    {
        return -1;
    }

    return (int) got;
}

static boolean FlacRewind(void *dec)
{
    flacdec_t *d = dec;

    d->pos = 0;

    return drflac_seek_to_pcm_frame(d->flac, 0) != 0;
}

static void FlacClose(void *dec)
{
    flacdec_t *d = dec;

    drflac_close(d->flac);
    free(d);
}

static size_t FlacBytes(void *dec)
{
    return ((flacdec_t *) dec)->bytes;
}

static const musdecoder_t flac_decoder =
{
    "flac",
    FlacProbe,
    FlacOpen,
    FlacRead,
    FlacRewind,
    FlacClose,
    FlacBytes,
};

const musdecoder_t *const musdecoders[] =
{
    &flac_decoder,
    NULL,
};
