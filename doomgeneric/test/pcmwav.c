// Test-only PCM sink: replaces i_alsapcm.c, models a device in real time and
// writes what it plays to a WAV (env PCMWAV). Used with headless-music.c.
//
// The device starts when start_threshold frames are queued and then plays
// at the stream rate, by CLOCK_MONOTONIC. Both modes of the sound module are
// supported:
//   blocking (thread mode)  PCM_Write sleeps until the frames fit. An
//        underrun restarts the stream and is counted in pcm->xruns; nothing
//        is played for the gap.
//   tick (PCM_SetParamsTick) writes do not block, an underrun plays silence
//        (the WAV shows it) and does not stop the stream, PCM_Delay says
//        how many frames are queued (negative after an underrun), PCM_Forward
//        skips the dry frames.
// PCMWAV_UNPACED=1 makes blocking writes return at once and play everything
// written, in order.
//
// At close it prints one line: frames played, xruns, silence gaps, writes
// and writes per second, which is the figure the wakeup comparison needs.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "i_alsapcm.h"

#define RING 65536                  // frames, a power of two

static FILE *wav;
static int16_t ring[2 * RING];
static unsigned long long appl;     // frames written
static unsigned long long flushed;  // frames played and in the WAV
static unsigned long long file_frames;
static unsigned long long gaps, silent_frames, writes;
static int in_gap, started, tick, unpaced;
static double t0, t_first, t_last;  // stream start, first and last write
static unsigned int cap, start_threshold, period;

static double Now(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static void Header(FILE *f, uint32_t rate, uint64_t frames)
{
    uint32_t v;
    uint16_t w;
    uint32_t dl = (uint32_t)(frames * 4);

    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); v = 36 + dl; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    w = 1; fwrite(&w, 2, 1, f); w = 2; fwrite(&w, 2, 1, f);
    v = rate; fwrite(&v, 4, 1, f); v = rate * 4; fwrite(&v, 4, 1, f);
    w = 4; fwrite(&w, 2, 1, f); w = 16; fwrite(&w, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dl, 4, 1, f);
}

// Frames played by now: those before the last (re)start plus the time since.

static unsigned long long hwbase;

static unsigned long long Hw(pcm_t *pcm)
{
    if (!started)
    {
        return hwbase;
    }

    return hwbase + (unsigned long long) ((Now() - t0) * pcm->rate);
}

// Play up to hw: what was written, then silence.

static void Flush(unsigned long long hw)
{
    static const int16_t zero[2] = { 0, 0 };

    while (flushed < hw)
    {
        const int16_t *p = flushed < appl ? &ring[2 * (flushed & (RING - 1))] : zero;

        if (flushed >= appl)
        {
            if (!in_gap)
            {
                in_gap = 1;
                ++gaps;
            }
            ++silent_frames;
        }

        fwrite(p, 4, 1, wav);
        ++file_frames;
        ++flushed;
    }
}

int PCM_Open(pcm_t *pcm, int card, int device)
{
    const char *p = getenv("PCMWAV");

    memset(pcm, 0, sizeof(*pcm));
    pcm->fd = 99;
    unpaced = getenv("PCMWAV_UNPACED") != NULL;
    wav = fopen(p ? p : "/out/pcm.wav", "wb");
    return wav ? 0 : -5;
}

static int SetParams(pcm_t *pcm, unsigned int rate, unsigned int period_frames,
                     unsigned int periods, int is_tick, unsigned int start)
{
    if (rate != 11025 && rate != 22050 && rate != 44100 && rate != 48000)
    {
        return -22;
    }

    pcm->rate = rate;
    pcm->period_frames = period_frames;
    pcm->periods = periods;
    cap = period_frames * periods;
    period = period_frames;
    tick = is_tick;
    start_threshold = is_tick ? start : period_frames;
    Header(wav, rate, 0);
    return 0;
}

int PCM_SetParams(pcm_t *pcm, unsigned int rate, unsigned int period_frames,
                  unsigned int periods)
{
    return SetParams(pcm, rate, period_frames, periods, 0, 0);
}

int PCM_SetParamsTick(pcm_t *pcm, unsigned int rate, unsigned int period_frames,
                      unsigned int periods, unsigned int start_frames)
{
    return SetParams(pcm, rate, period_frames, periods, 1, start_frames);
}

int PCM_SetNonblock(pcm_t *pcm, int on)
{
    return 0;
}

static void Sleep(double sec)
{
    struct timespec ts;

    if (sec <= 0)
    {
        return;
    }

    ts.tv_sec = (time_t) sec;
    ts.tv_nsec = (long) ((sec - ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

// Called after frames were queued.

static void MaybeStart(void)
{
    if (!started && appl - hwbase >= start_threshold)
    {
        started = 1;
        t0 = Now();
    }
}

int PCM_Delay(pcm_t *pcm, long *frames)
{
    unsigned long long hw = Hw(pcm);

    Flush(hw);
    *frames = (long) (appl - hw);
    return 0;
}

int PCM_Forward(pcm_t *pcm, unsigned long n)
{
    appl += n;
    return 0;
}

int PCM_Recover(pcm_t *pcm)
{
    ++pcm->xruns;
    return 0;
}

static void Put(const int16_t *frames, unsigned int n)
{
    unsigned int i;

    for (i = 0; i < n; ++i)
    {
        ring[2 * ((appl + i) & (RING - 1))] = frames[2 * i];
        ring[2 * ((appl + i) & (RING - 1)) + 1] = frames[2 * i + 1];
    }

    appl += n;
    in_gap = 0;
}

static void Note(void)
{
    t_last = Now();

    if (writes == 0)
    {
        t_first = t_last;
    }

    ++writes;
}

int PCM_WriteSome(pcm_t *pcm, const int16_t *frames, unsigned int count)
{
    unsigned long long hw = Hw(pcm);
    long long space;
    unsigned int n;

    Flush(hw);
    space = (long long) cap - (long long) (appl - hw);

    if (space <= 0)
    {
        return 0;
    }

    n = count < space ? count : (unsigned int) space;
    Put(frames, n);
    MaybeStart();
    Note();
    return (int) n;
}

int PCM_Write(pcm_t *pcm, const int16_t *frames, unsigned int count)
{
    Note();

    if (unpaced)
    {
        fwrite(frames, 4, count, wav);
        file_frames += count;
        appl += count;
        flushed = appl;
        return 0;
    }

    while (count > 0)
    {
        unsigned long long hw = Hw(pcm);
        long long space;
        unsigned int n;

        // An underrun: the stream stops, nothing is played for the gap, and
        // it restarts with the next period.

        if (started && hw > appl)
        {
            Flush(appl);
            pcm->xruns++;
            started = 0;
            hwbase = appl;
            hw = appl;
        }

        Flush(hw);
        space = (long long) cap - (long long) (appl - hw);

        if (space <= 0)
        {
            Sleep((double) (appl - hw - cap + (count < period ? count : period)) / pcm->rate);
            continue;
        }

        n = count < space ? count : (unsigned int) space;
        Put(frames, n);
        MaybeStart();
        frames += 2 * n;
        count -= n;
    }

    return 0;
}

void PCM_Drop(pcm_t *pcm) {}

void PCM_Close(pcm_t *pcm)
{
    if (wav)
    {
        double secs = t_last > t_first ? t_last - t_first : 0;

        if (!unpaced)
        {
            Flush(Hw(pcm));
        }

        Header(wav, pcm->rate ? pcm->rate : 22050, file_frames);
        fclose(wav);
        wav = NULL;
        fprintf(stderr, "pcmwav: %s %llu frames played, %u xruns, %llu silence gaps (%llu silent frames), "
                "%llu writes in %.1f s = %.1f writes/s\n",
                tick ? "tick" : unpaced ? "unpaced" : "blocking", file_frames, pcm->xruns, gaps,
                silent_frames, writes, secs, secs > 0 ? writes / secs : 0.0);
    }
}
