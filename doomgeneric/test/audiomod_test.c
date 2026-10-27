// Host test of the sound effects module (i_alsasound.c) on a fake PCM
// backend: the real module, the real mixer and the real fill planner, with
// the PCM layer, the WAD and the music source replaced.
//
// The fake device only moves when the test says so, one period at a time,
// so a scripted sequence of events gives the same result on every run:
//
//   thread mode  the test waits until the audio thread is blocked in
//                PCM_Write, applies the events due, then lets one period
//                play. The thread mixes the next period (with the events)
//                and blocks again.
//   tick mode    the test applies the events due, calls the module's
//                Update (which fills the queue), then lets one period play.
//
// Events are keyed by the number of frames mixed so far, which is the same
// number in both modes, so the played streams must be identical.
//
// Every scenario runs in a forked child, because the module keeps its state
// in statics. The child sends the played stream and the module's shutdown
// statistics to the parent through a pipe.
//
// audiomod_test               all scenarios of the module under test
// built with -DOLDMODULE      only the default scenario, to print the hash
//                             of the module as it was before this work
//                             (the Makefile builds it from git history)

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#include "../i_alsamusic.h"
#include "../i_alsapcm.h"
#include "../i_alsatick.h"
#include "../i_sound.h"
#include "../m_argv.h"

extern sound_module_t DG_sound_module;

// ---------------------------------------------------------------------
// Engine stubs.

char **myargv;
int myargc;

int M_CheckParm(char *check)
{
    int i;

    for (i = 1; i < myargc; ++i)
    {
        if (!strcmp(check, myargv[i]))
        {
            return i;
        }
    }
    return 0;
}

int M_CheckParmWithArgs(char *check, int num_args)
{
    int i;

    for (i = 1; i + num_args < myargc; ++i)
    {
        if (!strcmp(check, myargv[i]))
        {
            return i;
        }
    }
    return 0;
}

int M_snprintf(char *buf, size_t buf_len, const char *s, ...)
{
    va_list args;
    int result;

    va_start(args, s);
    result = vsnprintf(buf, buf_len, s, args);
    va_end(args);
    return result;
}

boolean M_StringCopy(char *dest, const char *src, size_t dest_size)
{
    snprintf(dest, dest_size, "%s", src);
    return strlen(src) < dest_size;
}

// Four sound lumps, "dsa" to "dsd": DMX header, 16 bytes of padding, the
// samples, 16 bytes of padding.

typedef struct
{
    const char *name;
    unsigned int rate;
    unsigned int len;
    unsigned char *lump;
} fakelump_t;

static fakelump_t lumps[] =
{
    { "dsa", 22050, 6000, NULL },
    { "dsb", 11025, 9000, NULL },
    { "dsc", 8000, 3000, NULL },
    { "dsd", 44100, 5000, NULL },
};

static void MakeLumps(void)
{
    int i;
    unsigned int k, n, total;

    for (i = 0; i < 4; ++i)
    {
        n = lumps[i].len;
        total = 8 + 16 + n + 16;
        lumps[i].lump = calloc(1, total);
        lumps[i].lump[0] = 3;
        lumps[i].lump[2] = lumps[i].rate & 255;
        lumps[i].lump[3] = lumps[i].rate >> 8;
        lumps[i].lump[4] = (n + 32) & 255;
        lumps[i].lump[5] = ((n + 32) >> 8) & 255;
        lumps[i].lump[6] = ((n + 32) >> 16) & 255;
        for (k = 0; k < n; ++k)
        {
            lumps[i].lump[24 + k] = (unsigned char) (k * (7 + 2 * i) + (k >> 5) * 13 + 3 * i);
        }
    }
}

int W_CheckNumForName(char *name)
{
    int i;

    for (i = 0; i < 4; ++i)
    {
        if (!strcmp(name, lumps[i].name))
        {
            return i;
        }
    }
    return -1;
}

int W_LumpLength(unsigned int lump)
{
    return 8 + 16 + lumps[lump].len + 16;
}

void W_ReadLump(unsigned int lump, void *dest)
{
    memcpy(dest, lumps[lump].lump, W_LumpLength(lump));
}

// The music source: a counter-based triangle wave, so that the samples do
// not depend on how the frames are split into calls. The test switches it.

static volatile int music_on;
static volatile int music_gain;
static unsigned int music_pos;

int I_ALSA_MusicRender(int16_t *out, int frames)
{
    int i;

    if (!music_on)
    {
        memset(out, 0, frames * sizeof(int16_t));
        return 0;
    }

    for (i = 0; i < frames; ++i, ++music_pos)
    {
        unsigned int ph = music_pos & 255;

        out[i] = (int16_t) ((ph < 128 ? (int) ph : 255 - (int) ph) * 200 - 12000);
    }

    return music_gain;
}

// ---------------------------------------------------------------------
// The fake PCM device.

#define FRING 65536                     // frames, more than any buffer here
#define MAXPLAY (1 << 20)               // frames recorded

static struct
{
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int16_t ring[2 * FRING];
    long long appl, hw;                 // frame counters
    unsigned long cap;
    unsigned int rate, period, periods;
    unsigned int start_frames;
    int tick;                           // set by PCM_SetParamsTick
    int nonblock;
    int dropped;
    int blocked;
    unsigned int blocked_gen;
    unsigned long long total_written;   // frames accepted by the device
    unsigned int accept_rate;           // 0: 22050 only; else only this one
    unsigned int max_period;            // 0: no limit; else grants min()
    unsigned int granted_periods_max;
    int16_t *played;
    long long nplayed;
    unsigned int writes;
    unsigned int xruns;
} F = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER };

static void Die(const char *msg)
{
    fprintf(stderr, "audiomod_test: %s\n", msg);
    _exit(3);
}

static void WaitCv(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 10;

    if (pthread_cond_timedwait(&F.cv, &F.mu, &ts) != 0)
    {
        Die("timeout waiting for the audio thread");
    }
}

int PCM_Open(pcm_t *pcm, int card, int device)
{
    memset(pcm, 0, sizeof(*pcm));
    pcm->fd = 42;
    return 0;
}

static int SetParams(pcm_t *pcm, unsigned int rate, unsigned int period,
                     unsigned int periods, int tick, unsigned int start)
{
    unsigned int want = F.accept_rate ? F.accept_rate : 22050;

    if (rate != want)
    {
        return -EINVAL;
    }

    if (F.max_period != 0 && period > F.max_period)
    {
        period = F.max_period;
    }

    F.rate = pcm->rate = rate;
    F.period = pcm->period_frames = period;
    F.periods = pcm->periods = periods;
    F.cap = (unsigned long) period * periods;
    F.tick = tick;
    F.start_frames = start;
    pcm->boundary = 1ul << 30;
    return 0;
}

int PCM_SetParams(pcm_t *pcm, unsigned int rate, unsigned int period,
                  unsigned int periods)
{
    return SetParams(pcm, rate, period, periods, 0, 0);
}

int PCM_SetParamsTick(pcm_t *pcm, unsigned int rate, unsigned int period,
                      unsigned int periods, unsigned int start)
{
    return SetParams(pcm, rate, period, periods, 1, start);
}

int PCM_SetNonblock(pcm_t *pcm, int on)
{
    F.nonblock = on;
    return 0;
}

int PCM_Delay(pcm_t *pcm, long *frames)
{
    pthread_mutex_lock(&F.mu);
    *frames = (long) (F.appl - F.hw);
    pthread_mutex_unlock(&F.mu);
    return 0;
}

int PCM_Forward(pcm_t *pcm, unsigned long frames)
{
    pthread_mutex_lock(&F.mu);
    F.appl += frames;
    pthread_mutex_unlock(&F.mu);
    return 0;
}

int PCM_Recover(pcm_t *pcm)
{
    ++pcm->xruns;
    return 0;
}

// Copy up to count frames in; the caller holds the lock.

static unsigned int PutLocked(const int16_t *frames, unsigned int count)
{
    long long used = F.appl - F.hw;
    long long space = (long long) F.cap - used;
    unsigned int n, i;

    if (space <= 0)
    {
        return 0;
    }

    n = count < space ? count : (unsigned int) space;

    for (i = 0; i < n; ++i)
    {
        unsigned int slot = (unsigned int) ((F.appl + i) % F.cap);

        F.ring[2 * slot] = frames[2 * i];
        F.ring[2 * slot + 1] = frames[2 * i + 1];
    }

    F.appl += n;
    F.total_written += n;
    ++F.writes;
    return n;
}

int PCM_WriteSome(pcm_t *pcm, const int16_t *frames, unsigned int count)
{
    unsigned int n;

    pthread_mutex_lock(&F.mu);
    n = PutLocked(frames, count);
    pthread_mutex_unlock(&F.mu);
    return (int) n;
}

int PCM_Write(pcm_t *pcm, const int16_t *frames, unsigned int count)
{
    unsigned int n;

    pthread_mutex_lock(&F.mu);

    while (count > 0)
    {
        n = PutLocked(frames, count);

        if (n == 0)
        {
            if (F.dropped)
            {
                pthread_mutex_unlock(&F.mu);
                return -EBADFD;
            }
            F.blocked = 1;
            ++F.blocked_gen;
            pthread_cond_broadcast(&F.cv);
            WaitCv();
            F.blocked = 0;
            continue;
        }

        frames += 2 * n;
        count -= n;
    }

    pthread_mutex_unlock(&F.mu);
    return 0;
}

void PCM_Drop(pcm_t *pcm)
{
    pthread_mutex_lock(&F.mu);
    F.dropped = 1;
    pthread_cond_broadcast(&F.cv);
    pthread_mutex_unlock(&F.mu);
}

void PCM_Close(pcm_t *pcm)
{
    pcm->fd = -1;
}

// The device plays n frames. What was written is played; beyond the write
// pointer it plays silence, and every played slot is overwritten with
// silence, as the kernel does with silence_size = boundary.

static void FakePlay(unsigned int n)
{
    unsigned int i;

    pthread_mutex_lock(&F.mu);

    for (i = 0; i < n; ++i)
    {
        long long pos = F.hw + i;
        unsigned int slot = (unsigned int) (pos % F.cap);
        int16_t l = 0, r = 0;

        if (pos < F.appl)
        {
            l = F.ring[2 * slot];
            r = F.ring[2 * slot + 1];
        }

        F.ring[2 * slot] = 0;
        F.ring[2 * slot + 1] = 0;

        if (F.nplayed < MAXPLAY)
        {
            F.played[2 * F.nplayed] = l;
            F.played[2 * F.nplayed + 1] = r;
            ++F.nplayed;
        }
    }

    F.hw += n;
    pthread_cond_broadcast(&F.cv);
    pthread_mutex_unlock(&F.mu);
}

// Wait until the audio thread is blocked in a write that it started after
// generation gen.

static void WaitBlocked(unsigned int gen)
{
    pthread_mutex_lock(&F.mu);

    while (!(F.blocked && F.blocked_gen != gen))
    {
        WaitCv();
    }

    pthread_mutex_unlock(&F.mu);
}

static unsigned int BlockedGen(void)
{
    unsigned int g;

    pthread_mutex_lock(&F.mu);
    g = F.blocked_gen;
    pthread_mutex_unlock(&F.mu);
    return g;
}

// ---------------------------------------------------------------------
// The script. idx is the number of the period (counted in periods of
// frames mixed) at which the event takes effect.

typedef struct
{
    int idx;
    char kind;      // S start, X stop, P params, M music on, m music off
    int ch, sfx, vol, sep;
} ev_t;

static const ev_t script[] =
{
    {  8, 'S',  0, 0, 100, 128 },
    { 10, 'S',  1, 1, 120,  40 },
    { 11, 'S',  2, 3, 127, 254 },
    { 13, 'P',  0, 0,  60,  10 },
    { 14, 'S',  3, 2,  90, 128 },
    { 16, 'X',  1, 0,   0,   0 },
    { 17, 'S',  1, 0, 127,   0 },
    { 20, 'M',  0, 0, 100,   0 },       // music on, gain 100
    { 22, 'S',  4, 1, 127, 128 },
    { 24, 'S',  0, 3,  80, 200 },       // restarts a playing channel
    { 27, 'P',  4, 0,  30, 250 },
    { 40, 'm',  0, 0,   0,   0 },       // music off
    { 55, 'M',  0, 0,  20,   0 },
    { 57, 'S',  5, 2,  70,  70 },
    { 66, 'm',  0, 0,   0,   0 },
    // all channels at once, full volume: the output clips
    { 72, 'S',  0, 0, 127, 128 }, { 72, 'S', 1, 0, 127, 128 },
    { 72, 'S',  2, 0, 127, 128 }, { 72, 'S', 3, 0, 127, 128 },
    { 72, 'S',  4, 1, 127, 128 }, { 72, 'S', 5, 1, 127, 128 },
    { 72, 'S',  6, 1, 127, 128 }, { 72, 'S', 7, 1, 127, 128 },
    { 72, 'S',  8, 0, 127, 128 }, { 72, 'S', 9, 0, 127, 128 },
    { 72, 'S', 10, 0, 127, 128 }, { 72, 'S', 11, 0, 127, 128 },
    { 72, 'S', 12, 3, 127, 128 }, { 72, 'S', 13, 3, 127, 128 },
    { 72, 'S', 14, 3, 127, 128 }, { 72, 'S', 15, 3, 127, 128 },
    { 76, 'X', 15, 0,   0,   0 },
    { 90, 'S',  2, 2, 127, 128 },
    { 1000, 0, 0, 0, 0, 0 },
};

static sfxinfo_t sfxs[4];

static void Apply(const ev_t *e)
{
    sound_module_t *m = &DG_sound_module;

    switch (e->kind)
    {
        case 'S': m->StartSound(&sfxs[e->sfx], e->ch, e->vol, e->sep); break;
        case 'X': m->StopSound(e->ch); break;
        case 'P': m->UpdateSoundParams(e->ch, e->vol, e->sep); break;
        case 'M': music_gain = e->vol; music_on = 1; break;
        case 'm': music_on = 0; break;
    }
}

static void ApplyDue(int idx)
{
    const ev_t *e;

    for (e = script; e->kind != 0; ++e)
    {
        if (e->idx == idx)
        {
            Apply(e);
        }
    }
}

// ---------------------------------------------------------------------
// One scenario, in a child process.

#define KPERIODS 100                    // periods played and compared

typedef struct
{
    int ok_init;
    unsigned int period, periods, tick;
    unsigned int rate;
    unsigned long cap;
    long long nplayed;
    long long hw_stall_start, written_stall_start, hw_stall_end;
    long delay_after_first;             // queued frames after the first update
    unsigned long long total_written;
    char stats[600];
    char init[600];
    int music_used;
} result_t;

typedef struct
{
    const char *name;
    const char *args;                   // space separated
    unsigned int accept_rate;
    unsigned int max_period;
    int stall_at;                       // iteration of the stall, -1: none
    int stall_frames;                   // frames the device plays in it
    int precache;                       // run the precache (deep fill) test
} scen_t;

static char *argbuf[32];

static void SetArgs(const char *args)
{
    static char copy[256];
    char *tok;

    myargc = 0;
    argbuf[myargc++] = "audiomod_test";
    snprintf(copy, sizeof(copy), "%s", args);

    for (tok = strtok(copy, " "); tok != NULL; tok = strtok(NULL, " "))
    {
        argbuf[myargc++] = tok;
    }

    argbuf[myargc] = NULL;
    myargv = argbuf;
}

static const char *Field(const char *line, const char *key, long *val)
{
    char pat[64];
    const char *p;

    snprintf(pat, sizeof(pat), " %s=", key);
    p = strstr(line, pat);

    if (p == NULL)
    {
        return NULL;
    }

    *val = strtol(p + strlen(pat), NULL, 10);
    return p;
}

static void Child(const scen_t *sc, int wfd)
{
    sound_module_t *m = &DG_sound_module;
    result_t res;
    FILE *cap;
    int i, it, k, saved;
    unsigned int gen;
    char line[600];

    memset(&res, 0, sizeof(res));
    MakeLumps();

    for (i = 0; i < 4; ++i)
    {
        sfxs[i].name[0] = 'a' + i;
        sfxs[i].name[1] = 0;
    }

    F.played = calloc(MAXPLAY, 2 * sizeof(int16_t));
    memset(F.ring, 0x5a, sizeof(F.ring));       // not silent: a stale-data trap
    F.accept_rate = sc->accept_rate;
    F.max_period = sc->max_period;
    SetArgs(sc->args);

    // Capture what the module prints.

    cap = tmpfile();
    fflush(stdout);
    saved = dup(1);
    dup2(fileno(cap), 1);

    res.ok_init = m->Init(true);

    fflush(stdout);
    dup2(saved, 1);
    close(saved);

    if (!res.ok_init)
    {
        write(wfd, &res, sizeof(res));
        _exit(0);
    }

    res.period = F.period;
    res.periods = F.periods;
    res.tick = F.tick;
    res.rate = F.rate;
    res.cap = F.cap;

    if (sc->precache)
    {
        // The deep fill before a stall that is known to come: the queue
        // holds the buffer minus one period, and a plain update adds nothing.

        long d;

        I_ALSA_Service(1);
        PCM_Delay(&(pcm_t){ 0 }, &d);
        res.delay_after_first = d;
        res.nplayed = 0;
        goto shutdown;
    }

    if (!F.tick)
    {
        // Thread mode: the device is full and the thread is blocked.

        gen = 0;
        WaitBlocked(gen);

        for (k = 0; k < KPERIODS; ++k)
        {
            gen = BlockedGen();
            ApplyDue(F.periods + k + 1);
            FakePlay(F.period);
            WaitBlocked(gen);
        }
    }
    else
    {
        for (it = 0; it < KPERIODS; ++it)
        {
            if (it == sc->stall_at)
            {
                res.hw_stall_start = F.hw;
                res.written_stall_start = F.total_written;
                FakePlay(sc->stall_frames);
                res.hw_stall_end = F.hw;
                continue;
            }

            // Events at the number of frames mixed so far.

            if (F.total_written % F.period == 0)
            {
                ApplyDue((int) (F.total_written / F.period));
            }

            m->Update();

            if (it == 0)
            {
                long d;

                PCM_Delay(&(pcm_t){ 0 }, &d);
                res.delay_after_first = d;
            }

            FakePlay(F.period);
        }
    }

shutdown:
    fflush(stdout);
    saved = dup(1);
    dup2(fileno(cap), 1);
    m->Shutdown();
    fflush(stdout);
    dup2(saved, 1);
    close(saved);

    rewind(cap);
    res.stats[0] = res.init[0] = 0;

    while (fgets(line, sizeof(line), cap) != NULL)
    {
        if (strstr(line, "I_ALSA_Stats:"))
        {
            snprintf(res.stats, sizeof(res.stats), "%s", line);
        }
        else if (strstr(line, "I_ALSA_InitSound:") && strstr(line, "mode="))
        {
            snprintf(res.init, sizeof(res.init), "%s", line);
        }
    }

    res.nplayed = F.nplayed;
    res.total_written = F.total_written;
    write(wfd, &res, sizeof(res));
    write(wfd, F.played, (size_t) F.nplayed * 4);
    _exit(0);
}

typedef struct
{
    result_t r;
    int16_t *played;
    unsigned int hash;
} run_t;

static unsigned int fnv(const void *d, size_t n)
{
    const unsigned char *p = d;
    unsigned int h = 2166136261u;

    while (n--)
    {
        h = (h ^ *p++) * 16777619u;
    }
    return h;
}

static int readall(int fd, void *buf, size_t n)
{
    char *p = buf;

    while (n > 0)
    {
        ssize_t got = read(fd, p, n);

        if (got <= 0)
        {
            return -1;
        }
        p += got;
        n -= got;
    }
    return 0;
}

static int Run(const scen_t *sc, run_t *out)
{
    int fds[2], status;
    pid_t pid;

    memset(out, 0, sizeof(*out));
    fflush(stdout);
    pipe(fds);
    pid = fork();

    if (pid == 0)
    {
        close(fds[0]);
        Child(sc, fds[1]);
    }

    close(fds[1]);

    if (readall(fds[0], &out->r, sizeof(out->r)) < 0)
    {
        waitpid(pid, &status, 0);
        close(fds[0]);
        return -1;
    }

    out->played = malloc((size_t) out->r.nplayed * 4 + 4);

    if (readall(fds[0], out->played, (size_t) out->r.nplayed * 4) < 0)
    {
        out->r.nplayed = 0;
    }

    close(fds[0]);
    waitpid(pid, &status, 0);
    out->hash = fnv(out->played, (size_t) out->r.nplayed * 4);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int nfail;

__attribute__((unused)) static void report(const char *name, int ok)
{
    printf("%-62s %s\n", name, ok ? "PASS" : "FAIL");
    nfail += !ok;
}

__attribute__((unused)) static long StatOf(const run_t *r, const char *key)
{
    long v = -12345;

    Field(r->r.stats, key, &v);
    return v;
}

static long long CountNonzero(const int16_t *a, long long from, long long to)
{
    long long i, n = 0;

    for (i = from; i < to; ++i)
    {
        n += a[2 * i] != 0 || a[2 * i + 1] != 0;
    }
    return n;
}

#ifdef OLDMODULE

int main(void)
{
    scen_t sc = { "default", "", 0, 0, -1, 0, 0 };
    run_t r;

    if (Run(&sc, &r) < 0)
    {
        printf("RESULT default FAILED\n");
        return 1;
    }
    printf("RESULT default hash=%08x frames=%lld nonzero=%lld\n", r.hash,
           r.r.nplayed, CountNonzero(r.played, 0, r.r.nplayed));
    return 0;
}

#else

static int Same(const run_t *a, const run_t *b)
{
    return a->r.nplayed == b->r.nplayed && a->hash == b->hash;
}

int main(void)
{
    // P = 441 frames: depths of 20, 60 and 100 ms are whole periods.

    scen_t base[] =
    {
        { "thread 441x4", "-audio-period 441", 0, 0, -1, 0, 0 },
        { "thread 441x4 idle", "-audio-period 441 -audio-idle", 0, 0, -1, 0, 0 },
        { "thread 441x2", "-audio-period 441 -audio-periods 2", 0, 0, -1, 0, 0 },
        { "thread 441x7 idle", "-audio-period 441 -audio-periods 7 -audio-idle", 0, 0, -1, 0, 0 },
        { "tick 441 depth 100", "-audio-mode tick -audio-period 441", 0, 0, -1, 0, 0 },
        { "tick 441 depth 100 idle", "-audio-mode tick -audio-period 441 -audio-idle", 0, 0, -1, 0, 0 },
        { "tick 441 depth 60", "-audio-mode tick -audio-period 441 -audio-depth 60", 0, 0, -1, 0, 0 },
        { "tick 441 depth 20", "-audio-mode tick -audio-period 441 -audio-depth 20", 0, 0, -1, 0, 0 },
        { "tick 441x3 depth 20 (periods raised)", "-audio-mode tick -audio-period 441 -audio-periods 3 -audio-depth 20", 0, 0, -1, 0, 0 },
    };
    enum { NB = sizeof(base) / sizeof(base[0]) };
    run_t rb[NB];
    run_t def, stall_short, stall_long, stall_long_idle;
    scen_t sdef = { "thread default", "", 0, 0, -1, 0, 0 };
    int i, ok;

    // The script gives audible output.

    for (i = 0; i < NB; ++i)
    {
        char name[128];

        if (Run(&base[i], &rb[i]) < 0)
        {
            snprintf(name, sizeof(name), "%s: runs", base[i].name);
            report(name, 0);
            return 1;
        }
    }

    ok = rb[0].r.nplayed == KPERIODS * 441
      && CountNonzero(rb[0].played, 0, rb[0].r.nplayed) > 20000;
    printf("  script: %lld frames, %lld non-silent, FNV %08x\n", rb[0].r.nplayed,
           CountNonzero(rb[0].played, 0, rb[0].r.nplayed), rb[0].hash);
    report("script plays and is audible", ok);

    for (i = 1; i < NB; ++i)
    {
        char name[160];

        snprintf(name, sizeof(name), "%s == thread 441x4 (FNV %08x)",
                 base[i].name, rb[i].hash);
        report(name, Same(&rb[i], &rb[0]));
    }

    // Counters.

    printf("  %s\n", rb[1].r.stats);
    printf("  %s\n", rb[4].r.stats);
    printf("  %s\n", rb[5].r.stats);
    report("idle shortcut used in thread mode (idle > 0)", StatOf(&rb[1], "idle") > 0);
    report("idle shortcut used in tick mode (idle > 0)", StatOf(&rb[5], "idle") > 0);
    report("no idle counted without -audio-idle", StatOf(&rb[0], "idle") == 0
           && StatOf(&rb[4], "idle") == 0);
    report("tick mode: no underrun, no short write",
           StatOf(&rb[4], "underruns") == 0 && StatOf(&rb[4], "short_writes") == 0);
    report("tick mode: thread fields are 0", StatOf(&rb[4], "thr_nvcsw") == 0
           && StatOf(&rb[4], "thr_utime_us") == 0);
    report("tick mode: first update fills the depth (2205 frames)",
           rb[4].r.delay_after_first == 2205);
    report("tick mode: depth 60 ms is 1323 frames", rb[6].r.delay_after_first == 1323);
    report("tick mode: 8 periods raised to 10 to hold twice the depth",
           rb[4].r.periods == 10);
    report("tick mode: 3 periods are enough for a depth of 1 period",
           rb[8].r.periods == 3);
    report("tick mode: min_delay seen, not negative",
           StatOf(&rb[4], "min_delay") >= 0);
    report("thread mode: writes are periods (frames = writes * 441)",
           StatOf(&rb[0], "frames") == StatOf(&rb[0], "writes") * 441);
    report("thread mode: init line names the mode",
           strstr(rb[0].r.init, "mode=thread") != NULL
           && strstr(rb[1].r.init, ", idle") != NULL
           && strstr(rb[4].r.init, "mode=tick, depth=2205 frames") != NULL);

    // The default is what it was before.

    if (Run(&sdef, &def) < 0)
    {
        report("thread default: runs", 0);
        return 1;
    }
    printf("RESULT default hash=%08x frames=%lld nonzero=%lld\n", def.hash,
           def.r.nplayed, CountNonzero(def.played, 0, def.r.nplayed));
    printf("  %s", def.r.init);
    report("default: 512 frames x 4 periods, thread mode",
           def.r.period == 512 && def.r.periods == 4 && !def.r.tick
           && strstr(def.r.init, "512 frames x 4 periods, mode=thread\n") != NULL);
    report("default plays the script", def.r.nplayed == KPERIODS * 512
           && CountNonzero(def.played, 0, def.r.nplayed) > 20000);

    // Forced stalls in tick mode, against the run without a stall. The
    // stall is one that outlasts the queue (200 periods of silence) or one
    // that does not (3 periods).

    {
        scen_t s1 = { "stall 3 periods", "-audio-mode tick -audio-period 441", 0, 0, 30, 3 * 441, 0 };
        scen_t s2 = { "stall 200 periods", "-audio-mode tick -audio-period 441", 0, 0, 30, 200 * 441, 0 };
        scen_t s3 = { "stall 200 periods idle", "-audio-mode tick -audio-period 441 -audio-idle", 0, 0, 30, 200 * 441, 0 };
        long long m0, g, len, j;
        int gap_zero, cont;

        if (Run(&s1, &stall_short) < 0 || Run(&s2, &stall_long) < 0
         || Run(&s3, &stall_long_idle) < 0)
        {
            report("stall scenarios run", 0);
            return 1;
        }

        // A stall shorter than the queue is inaudible: same stream as
        // without it, except that the stalled iteration plays 3 periods in
        // place of 1 (the stream is cut short by the iteration it skipped).

        m0 = stall_short.r.written_stall_start;
        len = rb[4].r.nplayed < stall_short.r.nplayed ? rb[4].r.nplayed : stall_short.r.nplayed;
        ok = 1;
        for (j = 0; j < len * 2; ++j)
        {
            ok &= stall_short.played[j] == rb[4].played[j];
        }
        printf("  short stall: %s", stall_short.r.stats);
        report("stall shorter than the queue: identical stream, no underrun",
               ok && StatOf(&stall_short, "underruns") == 0 && StatOf(&stall_short, "short_writes") == 0);

        // A long stall: [0, m0) as before, then silence, then the stream
        // goes on where it stopped (voices and music did not advance).

        {
            const run_t *sl[2] = { &stall_long, &stall_long_idle };
            const char *nm[2] = { "long stall", "long stall with -audio-idle" };
            int n;

            for (n = 0; n < 2; ++n)
            {
                char name[200];
                const run_t *s = sl[n];

                m0 = s->r.written_stall_start;
                g = s->r.hw_stall_end - m0;     // frames of silence played
                len = rb[4].r.nplayed - m0;
                if (len > s->r.nplayed - m0 - g)
                {
                    len = s->r.nplayed - m0 - g;
                }
                ok = 1;
                for (j = 0; j < m0 * 2; ++j)
                {
                    ok &= s->played[j] == rb[4].played[j];
                }
                gap_zero = CountNonzero(s->played, m0, m0 + g) == 0;
                cont = 1;
                for (j = 0; j < len * 2; ++j)
                {
                    cont &= s->played[2 * (m0 + g) + j] == rb[4].played[2 * m0 + j];
                }
                printf("  %s: mixed %lld frames before it, %lld frames of silence, %lld frames compared after\n",
                       nm[n], m0, g, len);
                printf("  %s", s->r.stats);

                snprintf(name, sizeof(name), "%s: before the gap equal to the no-stall run", nm[n]);
                report(name, ok);
                snprintf(name, sizeof(name), "%s: the gap is silent (never stale data)", nm[n]);
                report(name, g > 0 && gap_zero);
                snprintf(name, sizeof(name), "%s: after the gap the stream resumes", nm[n]);
                report(name, cont && len > 5 * 441);
                snprintf(name, sizeof(name), "%s: exactly 1 underrun, 0 short writes", nm[n]);
                report(name, StatOf(s, "underruns") == 1 && StatOf(s, "short_writes") == 0);
            }
        }
    }

    // Deep fill and switch parsing.

    {
        scen_t deep = { "deep", "-audio-mode tick -audio-period 441", 0, 0, -1, 0, 1 };
        run_t d;

        if (Run(&deep, &d) == 0)
        {
            printf("  deep fill: queue %ld frames of a %lu frame buffer\n",
                   d.r.delay_after_first, d.r.cap);
            report("deep fill queues the buffer minus one period",
                   d.r.delay_after_first == (long) (d.r.cap - d.r.period));
        }
        else
        {
            report("deep fill runs", 0);
        }
    }

    {
        struct
        {
            const char *args;
            unsigned int period, periods, tick;
            long first;
            const char *what;
        } t[] =
        {
            { "-audio-period 3", 512, 4, 0, -1, "-audio-period 3 falls back to 512" },
            { "-audio-period 5000", 512, 4, 0, -1, "-audio-period 5000 falls back to 512" },
            { "-audio-period x", 512, 4, 0, -1, "-audio-period x falls back to 512" },
            { "-audio-periods 1", 512, 4, 0, -1, "-audio-periods 1 falls back to 4" },
            { "-audio-periods 17", 512, 4, 0, -1, "-audio-periods 17 falls back to 4" },
            { "-audio-mode foo", 512, 4, 0, -1, "-audio-mode foo falls back to thread" },
            { "-audio-mode thread", 512, 4, 0, -1, "-audio-mode thread is the default" },
            { "-audio-period 1024 -audio-periods 2", 1024, 2, 0, -1, "-audio-period 1024 -audio-periods 2 is granted" },
            { "-audio-period 4096", 4096, 4, 0, -1, "-audio-period 4096 is granted" },
            { "-audio-period 64", 64, 4, 0, -1, "-audio-period 64 is granted" },
            { "-audio-mode tick", 1024, 8, 1, 2205, "tick: 1024 x 8 and 100 ms by default" },
            { "-audio-mode tick -audio-depth 0", 1024, 8, 1, 2205, "-audio-depth 0 falls back to 100 ms" },
            { "-audio-mode tick -audio-depth 19", 1024, 8, 1, 2205, "-audio-depth 19 falls back to 100 ms" },
            { "-audio-mode tick -audio-depth 1001", 1024, 8, 1, 2205, "-audio-depth 1001 falls back to 100 ms" },
            { "-audio-mode tick -audio-depth zz", 1024, 8, 1, 2205, "-audio-depth zz falls back to 100 ms" },
            { "-audio-mode tick -audio-depth 500", 1024, 16, 1, 8192, "-audio-depth 500: periods raised to 16, depth cut to half the buffer" },
        };
        int n;

        for (n = 0; n < (int) (sizeof(t) / sizeof(t[0])); ++n)
        {
            scen_t sc = { t[n].what, t[n].args, 0, 0, -1, 0, 0 };
            run_t r;
            int good;

            if (Run(&sc, &r) < 0)
            {
                report(t[n].what, 0);
                continue;
            }

            good = r.r.ok_init && r.r.period == t[n].period
                && r.r.tick == t[n].tick
                && r.r.periods == t[n].periods;

            if (t[n].tick)
            {
                good &= r.r.delay_after_first == t[n].first;
            }

            if (t[n].tick || t[n].period == 512)
            {
                // the mode runs the script without a crash
                good &= r.r.nplayed >= (long long) KPERIODS * 64;
            }

            report(t[n].what, good);
        }
    }

    // What the device grants is used: a device that caps the period, and
    // one that only takes another rate.

    {
        scen_t cap = { "cap", "-audio-period 2048 -audio-periods 2", 0, 1024, -1, 0, 0 };
        scen_t r44 = { "44100", "-audio-period 512", 44100, 0, -1, 0, 0 };
        scen_t r11 = { "11025", "-audio-period 512", 11025, 0, -1, 0, 0 };
        scen_t r44t = { "44100 tick", "-audio-mode tick", 44100, 0, -1, 0, 0 };
        run_t a, b, c, d;

        report("device caps the period: 2048 asked, 1024 granted and used",
               Run(&cap, &a) == 0 && a.r.period == 1024 && a.r.periods == 2
               && a.r.nplayed == KPERIODS * 1024);
        report("fallback to 44100: -audio-period 512 becomes 1024",
               Run(&r44, &b) == 0 && b.r.rate == 44100 && b.r.period == 1024);
        report("fallback to 11025: -audio-period 512 becomes 256",
               Run(&r11, &c) == 0 && c.r.rate == 11025 && c.r.period == 256);
        report("tick at 44100: 2048 x 8 by default, depth 4410 frames",
               Run(&r44t, &d) == 0 && d.r.rate == 44100 && d.r.period == 2048
               && d.r.delay_after_first == 4410);
    }

    printf("%s\n", nfail ? "SOME TESTS FAILED" : "ALL PASSED");
    return nfail != 0;
}

#endif
