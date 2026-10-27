//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
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
//	Sound effects through ALSA without libasound: sixteen voices mixed
//	by an integer mixer in one audio thread. The thread also mixes in the
//	music that i_alsamusic.c decodes (a hook, silent when absent).
//

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#include "deh_str.h"
#include "i_alsamusic.h"
#include "i_alsapcm.h"
#include "i_sndmix.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"

#include "doomtype.h"

// Only defined by <sys/resource.h> with _GNU_SOURCE; the value is Linux's.

#if defined(__linux__) && !defined(RUSAGE_THREAD)
#define RUSAGE_THREAD 1
#endif

#define NUM_VOICES 16

#define AUDIO_PERIOD_MIN 64
#define AUDIO_PERIOD_MAX 4096
#define AUDIO_PERIODS_MIN 2
#define AUDIO_PERIODS_MAX 16

typedef struct
{
    byte *lump;             // whole lump, malloc'd, never freed while the thread runs
    const byte *data;       // lump + 24
    const byte *end;        // data + (length - 32)
    unsigned int step;      // SNDMIX_Step(lump rate, output rate)
} alsasfx_t;

typedef struct
{
    unsigned int rate;
    unsigned int period;
    unsigned int periods;
} pcmmode_t;

// Default order, first success wins.

static const pcmmode_t default_modes[] =
{
    { 22050, 512, 4 },
    { 44100, 1024, 4 },
    { 11025, 256, 4 },
    { 48000, 1024, 4 },
};

static sndvoice_t voices[NUM_VOICES];
static pthread_mutex_t voice_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t mix_thread;
static boolean sound_initialized = false;
static boolean use_sfx_prefix;
static int mix_quit;                    // read and written under voice_lock
static int mix_error;                   // set by the thread before it exits
static pcm_t pcm;
static int32_t *mix_acc;                // 2 * period frames
static int16_t *mix_out;                // 2 * period frames
static int16_t *music_out;              // period frames, mono

// Run-time options (-audio-period, -audio-periods), 0 = not given.

static unsigned int opt_period;
static unsigned int opt_periods;

// Counters for the shutdown line. The audio thread owns them while it runs;
// the main thread reads them after pthread_join.

static unsigned int stat_writes;
static unsigned long long stat_frames;
static long thr_utime_us, thr_stime_us, thr_nvcsw, thr_nivcsw;

// Not used: there is no resampling library, and no float math.

int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

// Without i_alsamusic.o there is no music: the hook gives silence.

__attribute__((weak)) int I_ALSA_MusicRender(int16_t *out, int frames)
{
    return 0;
}

int I_ALSA_SoundRate(void)
{
    return sound_initialized ? (int) pcm.rate : 0;
}

// Get the lump name of a sound effect, following links.

static void GetSfxLumpName(sfxinfo_t *sfx, char *buf, size_t buf_len)
{
    // Linked sfx lumps? Get the lump number for the sound linked to.

    if (sfx->link != NULL)
    {
        sfx = sfx->link;
    }

    // Doom adds a DS* prefix to sound lumps; Heretic and Hexen don't
    // do this.

    if (use_sfx_prefix)
    {
        M_snprintf(buf, buf_len, "ds%s", DEH_String(sfx->name));
    }
    else
    {
        M_StringCopy(buf, DEH_String(sfx->name), buf_len);
    }
}

// Load a sound lump into malloc'd memory, not the zone: the data is
// read by the audio thread and must never move or be purged.

static boolean CacheSFX(sfxinfo_t *sfxinfo)
{
    char namebuf[9];
    int lumpnum;
    int lumplen;
    unsigned int samplerate;
    unsigned int length;
    byte *lump;
    alsasfx_t *snd;

    GetSfxLumpName(sfxinfo, namebuf, sizeof(namebuf));

    lumpnum = W_CheckNumForName(namebuf);

    if (lumpnum < 0)
    {
        return false;
    }

    lumplen = W_LumpLength(lumpnum);

    // Check the header, and ignore very short sound lumps.

    if (lumplen < 8)
    {
        return false;
    }

    lump = malloc(lumplen);

    if (lump == NULL)
    {
        return false;
    }

    W_ReadLump(lumpnum, lump);

    if (lump[0] != 0x03 || lump[1] != 0x00)
    {
        free(lump);
        return false;
    }

    samplerate = (lump[3] << 8) | lump[2];
    length = (lump[7] << 24) | (lump[6] << 16) | (lump[5] << 8) | lump[4];

    // If the header specifies that the length of the sound is greater than
    // the length of the lump itself, this is an invalid sound lump.
    // We also discard sound lumps that are less than 49 samples long.

    if (samplerate == 0 || length > (unsigned int) lumplen - 8 || length <= 48)
    {
        free(lump);
        return false;
    }

    snd = malloc(sizeof(alsasfx_t));

    if (snd == NULL)
    {
        free(lump);
        return false;
    }

    // The DMX header is 8 bytes, followed by 16 bytes of padding, and 16
    // bytes of padding at the end.

    snd->lump = lump;
    snd->data = lump + 24;
    snd->end = snd->data + (length - 32);
    snd->step = SNDMIX_Step(samplerate, pcm.rate);

    sfxinfo->driver_data = snd;

    return true;
}

static alsasfx_t *GetSfxData(sfxinfo_t *sfxinfo)
{
    if (sfxinfo->link != NULL)
    {
        sfxinfo = sfxinfo->link;
    }

    if (sfxinfo->driver_data == NULL)
    {
        CacheSFX(sfxinfo);
    }

    return sfxinfo->driver_data;
}

static int I_ALSA_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];

    GetSfxLumpName(sfx, namebuf, sizeof(namebuf));

    // A missing lump gives silence, not an error.

    return W_CheckNumForName(namebuf);
}

static void I_ALSA_UpdateSound(void)
{
    // The audio thread does the work.
}

static void I_ALSA_UpdateSoundParams(int handle, int vol, int sep)
{
    sndvoice_t gains;

    if (!sound_initialized || handle < 0 || handle >= NUM_VOICES)
    {
        return;
    }

    SNDMIX_SetGains(&gains, vol, sep);

    pthread_mutex_lock(&voice_lock);
    voices[handle].left = gains.left;
    voices[handle].right = gains.right;
    pthread_mutex_unlock(&voice_lock);
}

static int I_ALSA_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
    alsasfx_t *snd;
    sndvoice_t gains;

    if (!sound_initialized || channel < 0 || channel >= NUM_VOICES)
    {
        return -1;
    }

    snd = GetSfxData(sfxinfo);

    if (snd == NULL)
    {
        return -1;
    }

    SNDMIX_SetGains(&gains, vol, sep);

    pthread_mutex_lock(&voice_lock);
    voices[channel].pos = snd->data;
    voices[channel].end = snd->end;
    voices[channel].frac = 0;
    voices[channel].step = snd->step;
    voices[channel].left = gains.left;
    voices[channel].right = gains.right;
    pthread_mutex_unlock(&voice_lock);

    return channel;
}

static void I_ALSA_StopSound(int handle)
{
    if (!sound_initialized || handle < 0 || handle >= NUM_VOICES)
    {
        return;
    }

    pthread_mutex_lock(&voice_lock);
    voices[handle].pos = NULL;
    pthread_mutex_unlock(&voice_lock);
}

static boolean I_ALSA_SoundIsPlaying(int handle)
{
    boolean playing;

    if (!sound_initialized || handle < 0 || handle >= NUM_VOICES)
    {
        return false;
    }

    pthread_mutex_lock(&voice_lock);
    playing = voices[handle].pos != NULL;
    pthread_mutex_unlock(&voice_lock);

    return playing;
}

// Load all the sound effects, so that nothing is read from the WAD while
// the game runs.

static void I_ALSA_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    int i, count = 0;
    size_t bytes = 0;

    if (!sound_initialized)
    {
        return;
    }

    for (i = 0; i < num_sounds; ++i)
    {
        if (sounds[i].link == NULL && CacheSFX(&sounds[i]))
        {
            alsasfx_t *snd = sounds[i].driver_data;

            ++count;
            bytes += snd->end - snd->data;
        }
    }

    printf("I_ALSA_PrecacheSounds: %d sounds, %u bytes\n", count,
           (unsigned int) bytes);
}

// The audio thread: mix one period, write it, repeat. It never calls into
// the engine and never prints.

static void *MixThread(void *arg)
{
    int err, gain;

    for (;;)
    {
        // Decode and fetch the music first: it takes its own lock, which is
        // never held together with voice_lock.

        gain = I_ALSA_MusicRender(music_out, pcm.period_frames);

        pthread_mutex_lock(&voice_lock);

        if (mix_quit)
        {
            pthread_mutex_unlock(&voice_lock);
            break;
        }

        SNDMIX_Mix(voices, NUM_VOICES, mix_acc, pcm.period_frames);
        pthread_mutex_unlock(&voice_lock);

        if (gain > 0)
        {
            SNDMIX_MixMono(music_out, gain, mix_acc, pcm.period_frames);
        }

        SNDMIX_Output(mix_acc, mix_out, pcm.period_frames);

        err = PCM_Write(&pcm, mix_out, pcm.period_frames);

        if (err < 0)
        {
            mix_error = -err;
            break;
        }

        ++stat_writes;
        stat_frames += pcm.period_frames;
    }

#ifdef RUSAGE_THREAD
    {
        struct rusage ru;

        if (getrusage(RUSAGE_THREAD, &ru) == 0)
        {
            thr_utime_us = (long) ru.ru_utime.tv_sec * 1000000 + ru.ru_utime.tv_usec;
            thr_stime_us = (long) ru.ru_stime.tv_sec * 1000000 + ru.ru_stime.tv_usec;
            thr_nvcsw = ru.ru_nvcsw;
            thr_nivcsw = ru.ru_nivcsw;
        }
    }
#endif

    return NULL;
}

// The period for a fallback rate when -audio-period is given: the value
// applies to 22050 Hz and is scaled by shifts like the default table.

static unsigned int ScalePeriod(unsigned int period, unsigned int rate)
{
    if (rate >= 44100)
    {
        period <<= 1;
    }
    else if (rate <= 11025)
    {
        period >>= 1;
    }

    if (period < AUDIO_PERIOD_MIN)
    {
        period = AUDIO_PERIOD_MIN;
    }
    if (period > AUDIO_PERIOD_MAX)
    {
        period = AUDIO_PERIOD_MAX;
    }

    return period;
}

// Try one mode, then the rest of the default list if no rate is forced.

static int OpenPCM(int card, int device, int forced_rate)
{
    int err, i;
    unsigned int period, periods;

    err = PCM_Open(&pcm, card, device);

    if (err < 0)
    {
        return err;
    }

    if (forced_rate > 0)
    {
        period = 64;

        while (period * 2 <= (unsigned int) forced_rate * 512 / 22050)
        {
            period *= 2;
        }

        if (opt_period != 0)
        {
            period = opt_period;
        }

        periods = opt_periods != 0 ? opt_periods : 4;

        return PCM_SetParams(&pcm, forced_rate, period, periods);
    }

    err = -EINVAL;

    for (i = 0; i < arrlen(default_modes); ++i)
    {
        period = default_modes[i].period;

        if (opt_period != 0)
        {
            period = ScalePeriod(opt_period, default_modes[i].rate);
        }

        periods = opt_periods != 0 ? opt_periods : default_modes[i].periods;

        err = PCM_SetParams(&pcm, default_modes[i].rate, period, periods);

        if (err == 0)
        {
            break;
        }
    }

    return err;
}

// Read a numeric switch. A value out of range prints one line and is
// ignored: the game must still run.

static unsigned int ParseAudioArg(const char *name, unsigned int min,
                                  unsigned int max)
{
    int i = M_CheckParmWithArgs((char *) name, 1);
    long val;
    char *end;

    if (i <= 0)
    {
        return 0;
    }

    val = strtol(myargv[i + 1], &end, 10);

    if (*end != '\0' || val < (long) min || val > (long) max)
    {
        printf("I_ALSA_InitSound: %s %s: need %u to %u, ignored\n", name,
               myargv[i + 1], min, max);
        return 0;
    }

    return (unsigned int) val;
}

static boolean I_ALSA_InitSound(boolean _use_sfx_prefix)
{
    int card = 0, device = 0, forced_rate = 0;
    int i, err;
    sigset_t all, old;

    use_sfx_prefix = _use_sfx_prefix;

    //!
    // @arg <card>,<device>
    //
    // Use the given ALSA playback device for sound effects. The
    // default is 0,0.
    //

    i = M_CheckParmWithArgs("-sndpcm", 1);

    if (i > 0)
    {
        sscanf(myargv[i + 1], "%d,%d", &card, &device);
    }

    //!
    // @arg <hz>
    //
    // Play sound effects at this sample rate only, instead of trying
    // 22050, 44100, 11025 and 48000 Hz in turn.
    //

    i = M_CheckParmWithArgs("-sndrate", 1);

    if (i > 0)
    {
        forced_rate = atoi(myargv[i + 1]);
    }

    //!
    // @arg <frames>
    //
    // Ask the sound device for periods of this many frames (64 to 4096)
    // instead of the default. The value applies to 22050 Hz; other rates
    // use it scaled in powers of two. The size the device grants is
    // printed. Larger periods mean fewer wakeups of the audio thread and a
    // longer delay before a sound is heard.
    //

    opt_period = ParseAudioArg("-audio-period", AUDIO_PERIOD_MIN,
                               AUDIO_PERIOD_MAX);

    //!
    // @arg <n>
    //
    // Ask the sound device for this many periods (2 to 16) instead of 4.
    //

    opt_periods = ParseAudioArg("-audio-periods", AUDIO_PERIODS_MIN,
                                AUDIO_PERIODS_MAX);

    err = OpenPCM(card, device, forced_rate);

    if (err < 0)
    {
        printf("I_ALSA_InitSound: hw:%d,%d: %s\n", card, device,
               strerror(-err));
        PCM_Close(&pcm);
        return false;
    }

    // calloc: SNDMIX_Mix needs an all-zero accumulator on entry.

    mix_acc = calloc(2 * pcm.period_frames, sizeof(int32_t));
    mix_out = malloc(2 * pcm.period_frames * sizeof(int16_t));
    music_out = calloc(pcm.period_frames, sizeof(int16_t));

    if (mix_acc == NULL || mix_out == NULL || music_out == NULL)
    {
        printf("I_ALSA_InitSound: out of memory\n");
        goto fail;
    }

    // Prime one period of silence, which also starts the stream.

    memset(mix_out, 0, 2 * pcm.period_frames * sizeof(int16_t));
    err = PCM_Write(&pcm, mix_out, pcm.period_frames);

    if (err < 0)
    {
        printf("I_ALSA_InitSound: hw:%d,%d: %s\n", card, device,
               strerror(-err));
        goto fail;
    }

    stat_writes = 1;
    stat_frames = pcm.period_frames;

    // Block all signals around thread creation, so that the new thread
    // inherits a full mask and signals always reach the main thread.

    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, &old);
    err = pthread_create(&mix_thread, NULL, MixThread, NULL);
    pthread_sigmask(SIG_SETMASK, &old, NULL);

    if (err != 0)
    {
        printf("I_ALSA_InitSound: pthread_create: %s\n", strerror(err));
        goto fail;
    }

    sound_initialized = true;

    printf("I_ALSA_InitSound: hw:%d,%d %u Hz, %u frames x %u periods, mode=thread\n",
           card, device, pcm.rate, pcm.period_frames, pcm.periods);

    return true;

fail:
    free(mix_acc);
    free(mix_out);
    free(music_out);
    mix_acc = NULL;
    mix_out = NULL;
    music_out = NULL;
    PCM_Close(&pcm);
    return false;
}

static void I_ALSA_ShutdownSound(void)
{
    if (!sound_initialized)
    {
        return;
    }

    pthread_mutex_lock(&voice_lock);
    mix_quit = 1;
    pthread_mutex_unlock(&voice_lock);

    // Wakes a write that is blocked.

    PCM_Drop(&pcm);
    pthread_join(mix_thread, NULL);
    PCM_Close(&pcm);

    sound_initialized = false;

    // The sound data is not freed: the process is exiting.

    printf("I_ALSA_ShutdownSound: %u xruns\n", pcm.xruns);
    printf("I_ALSA_Stats: mode=thread rate=%u period=%u periods=%u writes=%u "
           "frames=%llu idle=0 xruns=%u underruns=0 short_writes=0 "
           "min_delay=-1 thr_utime_us=%ld thr_stime_us=%ld thr_nvcsw=%ld "
           "thr_nivcsw=%ld\n",
           pcm.rate, pcm.period_frames, pcm.periods, stat_writes, stat_frames,
           pcm.xruns, thr_utime_us, thr_stime_us, thr_nvcsw, thr_nivcsw);

    if (mix_error != 0 && mix_error != EBADFD)
    {
        printf("I_ALSA_ShutdownSound: audio thread stopped: %s\n",
               strerror(mix_error));
    }
}

static snddevice_t sound_alsa_devices[] =
{
    SNDDEVICE_SB,
    SNDDEVICE_PAS,
    SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS,
    SNDDEVICE_AWE32,
};

sound_module_t DG_sound_module =
{
    sound_alsa_devices,
    arrlen(sound_alsa_devices),
    I_ALSA_InitSound,
    I_ALSA_ShutdownSound,
    I_ALSA_GetSfxLumpNum,
    I_ALSA_UpdateSound,
    I_ALSA_UpdateSoundParams,
    I_ALSA_StartSound,
    I_ALSA_StopSound,
    I_ALSA_SoundIsPlaying,
    I_ALSA_PrecacheSounds,
};
