// Host test of the integer sound effects mixer (i_sndmix.c).
// The mixer source is #included, not copied. The reference mixer below is
// independent: 64-bit arithmetic, absolute frame numbers, no shared state.

#include "../i_sndmix.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define OUT_RATE 22050
#define PERIOD   512
#define MAXV     16

static const char *wadpath = "freedoom1.wad";
static FILE *wadf;
static int numlumps;
static unsigned char *dir;

static unsigned int rd32(const unsigned char *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (unsigned int) p[3] << 24;
}

static void wad_open(void)
{
    unsigned char h[12];

    wadf = fopen(wadpath, "rb");
    if (!wadf || fread(h, 1, 12, wadf) != 12 || memcmp(h + 1, "WAD", 3))
    {
        fprintf(stderr, "cannot read %s\n", wadpath);
        exit(2);
    }
    numlumps = rd32(h + 4);
    dir = malloc(16 * numlumps);
    fseek(wadf, rd32(h + 8), SEEK_SET);
    if (fread(dir, 16, numlumps, wadf) != (size_t) numlumps)
    {
        exit(2);
    }
}

typedef struct
{
    char name[9];
    int rate;
    int len;            // samples
    byte *pcm;
} sfx_t;

// Parse a DMX lump the Chocolate way: 03 00, rate, length; PCM at 24,
// length-32 bytes.

static int wad_sfx(const char *name, sfx_t *s)
{
    int i;

    for (i = 0; i < numlumps; ++i)
    {
        if (!strncmp((char *) dir + 16 * i + 8, name, 8))
        {
            unsigned int pos = rd32(dir + 16 * i), size = rd32(dir + 16 * i + 4);
            unsigned char *l = malloc(size);
            unsigned int length;

            fseek(wadf, pos, SEEK_SET);
            if (fread(l, 1, size, wadf) != size || size < 8 || l[0] != 3 || l[1] != 0)
            {
                return 0;
            }
            length = rd32(l + 4);
            if (length > size - 8 || length <= 48)
            {
                return 0;
            }
            strcpy(s->name, name);
            s->rate = l[2] | l[3] << 8;
            s->len = length - 32;
            s->pcm = malloc(s->len);
            memcpy(s->pcm, l + 24, s->len);
            free(l);
            return 1;
        }
    }
    return 0;
}

static sfx_t *load(const char *name)
{
    sfx_t *s = calloc(1, sizeof(*s));

    if (!wad_sfx(name, s))
    {
        fprintf(stderr, "lump %s missing\n", name);
        exit(2);
    }
    return s;
}

static sfx_t *synth(const char *name, int rate, int len, int kind)
{
    sfx_t *s = calloc(1, sizeof(*s));
    int i;

    strcpy(s->name, name);
    s->rate = rate;
    s->len = len;
    s->pcm = malloc(len);
    for (i = 0; i < len; ++i)
    {
        switch (kind)
        {
        case 0: s->pcm[i] = 255; break;
        case 1: s->pcm[i] = 0; break;
        default: s->pcm[i] = (i * 7 + (i >> 8)) & 255; break;  // marks every index
        }
    }
    return s;
}

// ---------------------------------------------------------------------
// Reference mixer. Sample index of output frame f of a voice started at
// frame f0 with 16.16 step st: idx = (k * st) >> 16, k = f - f0, computed
// in 64 bit. The voice is heard while idx < len and f < stop.

typedef struct
{
    const sfx_t *sfx;
    long long start, stop;      // output frames; stop = -1: never stopped
    long long step;
    long long gl, gr;
} refvoice_t;

static refvoice_t ref[MAXV];
static int nref;

static void ref_mix(int16_t *out, long long f0, int frames)
{
    long long f;
    int v;

    for (f = f0; f < f0 + frames; ++f)
    {
        long long al = 0, ar = 0;

        for (v = 0; v < nref; ++v)
        {
            refvoice_t *r = &ref[v];
            long long k, idx;

            if (!r->sfx || f < r->start || (r->stop >= 0 && f >= r->stop))
            {
                continue;
            }
            k = f - r->start;
            idx = (k * r->step) >> 16;
            if (idx >= r->sfx->len)
            {
                continue;
            }
            al += (r->sfx->pcm[idx] - 128) * r->gl;
            ar += (r->sfx->pcm[idx] - 128) * r->gr;
        }
        // arithmetic shift: floor division by 2, as the mixer does
        al = (al >= 0) ? al / 2 : -((-al + 1) / 2);
        ar = (ar >= 0) ? ar / 2 : -((-ar + 1) / 2);
        if (al > 32767) al = 32767;
        if (al < -32768) al = -32768;
        if (ar > 32767) ar = 32767;
        if (ar < -32768) ar = -32768;
        out[2 * (f - f0)] = (int16_t) al;
        out[2 * (f - f0) + 1] = (int16_t) ar;
    }
}

// ---------------------------------------------------------------------
// Driver: the same event list feeds the real mixer and the reference.

typedef struct
{
    int period;             // applied before this period is mixed
    int slot;
    const sfx_t *sfx;       // NULL: stop
    int vol, sep;
    int out_rate;
} event_t;

static sndvoice_t voices[MAXV];

static unsigned int fnv(unsigned int h, const void *d, size_t n)
{
    const unsigned char *p = d;
    while (n--)
    {
        h = (h ^ *p++) * 16777619u;
    }
    return h;
}

typedef struct
{
    int16_t *out;           // everything that was rendered
    int frames;
    int mismatches;
    long long first_bad;
    unsigned int hash;
} result_t;

static result_t run(const event_t *ev, int nev, int periods, int last_frames)
{
    result_t res;
    int32_t acc[2 * PERIOD] = { 0 };  // SNDMIX_Mix needs zero on entry
    int16_t buf[2 * PERIOD], rbuf[2 * PERIOD];
    int p, e, i, frames;
    long long total = 0;

    memset(voices, 0, sizeof(voices));
    memset(ref, 0, sizeof(ref));
    nref = MAXV;
    res.out = malloc(sizeof(int16_t) * 2 * PERIOD * periods);
    res.mismatches = 0;
    res.first_bad = -1;
    res.hash = 2166136261u;

    for (p = 0; p < periods; ++p)
    {
        for (e = 0; e < nev; ++e)
        {
            const event_t *x = &ev[e];
            sndvoice_t *v = &voices[x->slot];
            refvoice_t *r = &ref[x->slot];

            if (x->period != p)
            {
                continue;
            }
            if (r->sfx && r->stop < 0)
            {
                r->stop = (long long) p * PERIOD;
            }
            if (!x->sfx)
            {
                v->pos = NULL;
                continue;
            }
            v->pos = x->sfx->pcm;
            v->end = x->sfx->pcm + x->sfx->len;
            v->frac = 0;
            v->step = SNDMIX_Step(x->sfx->rate, x->out_rate);
            SNDMIX_SetGains(v, x->vol, x->sep);

            // the reference uses its own arithmetic for step and gains
            // (64-bit, truncating division) and is checked against the module
            nref = MAXV;
            ref[x->slot] = (refvoice_t) { x->sfx, (long long) p * PERIOD, -1,
                ((long long) x->sfx->rate << 16) / x->out_rate,
                ((254LL - x->sep) * x->vol) / 127, ((long long) x->sep * x->vol) / 127 };
            if (ref[x->slot].gl > 254) ref[x->slot].gl = 254;
            if (ref[x->slot].gr > 254) ref[x->slot].gr = 254;
            if ((long long) v->step != ref[x->slot].step
             || v->left != ref[x->slot].gl || v->right != ref[x->slot].gr)
            {
                printf("  step/gain mismatch slot %d: %u/%d/%d vs %lld/%lld/%lld\n",
                       x->slot, v->step, v->left, v->right,
                       ref[x->slot].step, ref[x->slot].gl, ref[x->slot].gr);
                res.mismatches++;
            }
        }

        frames = (p == periods - 1) ? last_frames : PERIOD;
        SNDMIX_Mix(voices, MAXV, acc, frames);
        SNDMIX_Output(acc, buf, frames);
        ref_mix(rbuf, (long long) p * PERIOD, frames);

        for (i = 0; i < 2 * frames; ++i)
        {
            if (buf[i] != rbuf[i])
            {
                if (res.first_bad < 0)
                {
                    res.first_bad = total + i / 2;
                }
                res.mismatches++;
            }
        }
        memcpy(res.out + 2 * total, buf, sizeof(int16_t) * 2 * frames);
        res.hash = fnv(res.hash, buf, sizeof(int16_t) * 2 * frames);
        total += frames;
    }
    res.frames = total;
    return res;
}

static int nfail;

static void report(const char *name, int ok, const char *fmt, ...)
{
    printf("%-34s %s\n", name, ok ? "PASS" : "FAIL");
    (void) fmt;
    if (!ok)
    {
        nfail++;
    }
}

static int count_nonzero(const int16_t *o, int from, int to)
{
    int i, n = 0;
    for (i = 2 * from; i < 2 * to; ++i)
    {
        n += o[i] != 0;
    }
    return n;
}

static int peak(const int16_t *o, int from, int to)
{
    int i, m = 0;
    for (i = 2 * from; i < 2 * to; ++i)
    {
        if (abs(o[i]) > m) m = abs(o[i]);
    }
    return m;
}

#define ROUND_UP(x) (((x) + PERIOD - 1) / PERIOD)

static void test_voices(int n)
{
    static const char *names[8] = { "DSPISTOL", "DSSHOTGN", "DSSGCOCK", "DSDOROPN",
                                    "DSPLPAIN", "DSPOSIT1", "DSBAREXP", "DSITEMUP" };
    static const int vols[8] = { 127, 100, 64, 90, 127, 33, 120, 77 };
    static const int seps[8] = { 128, 0, 254, 60, 200, 128, 17, 240 };
    event_t ev[8];
    int i, maxlen = 0, ok, r11 = 0, r22 = 0;
    char title[64];
    result_t res;

    for (i = 0; i < n; ++i)
    {
        sfx_t *s = load(names[i]);
        int l = (int) (((long long) s->len << 16) / (((long long) s->rate << 16) / OUT_RATE)) + 1;

        r11 += s->rate == 11025;
        r22 += s->rate == 22050;
        if (l > maxlen) maxlen = l;
        ev[i] = (event_t) { 0, i, s, vols[i], seps[i], OUT_RATE };
        printf("  voice %d: %s %d Hz %d samples vol %d sep %d\n", i, s->name, s->rate, s->len, vols[i], seps[i]);
    }
    res = run(ev, n, ROUND_UP(maxlen), maxlen - (ROUND_UP(maxlen) - 1) * PERIOD);
    ok = res.mismatches == 0 && peak(res.out, 0, res.frames) > 0;
    printf("  %d frames, %d mismatches, peak %d, hash %08x (11025 Hz lumps: %d, 22050 Hz lumps: %d)\n",
           res.frames, res.mismatches, peak(res.out, 0, res.frames), res.hash, r11, r22);
    sprintf(title, "bit-exact %d voice%s", n, n == 1 ? "" : "s");
    report(title, ok, "");
}

// Voices with 16-bit-visible results for the edge tests use synthetic data.

static void test_clamp(void)
{
    sfx_t *hi = synth("hi", OUT_RATE, 4096, 0), *lo = synth("lo", OUT_RATE, 4096, 1);
    event_t ev[16];
    result_t res;
    int i, ok;

    // one voice at full scale: 127 * 254 >> 1 = 16129, no clamp
    ev[0] = (event_t) { 0, 0, hi, 127, 254, OUT_RATE };
    res = run(ev, 1, 2, PERIOD);
    ok = res.mismatches == 0 && res.out[0] == 0 && res.out[1] == 16129;
    printf("  1 voice, sep 254: L %d R %d (expect 0, 16129), mismatches %d\n", res.out[0], res.out[1], res.mismatches);

    // 4 positive voices on both channels: sep 127 gives gl = gr = 127 at vol 127
    // use sep 127 -> gl=127, gr=127; 16 voices * 127 * 127 >> 1 = 129032 -> clamp
    for (i = 0; i < 16; ++i)
    {
        ev[i] = (event_t) { 0, i, hi, 127, 127, OUT_RATE };
    }
    res = run(ev, 16, 2, PERIOD);
    ok &= res.mismatches == 0 && res.out[0] == 32767 && res.out[1] == 32767;
    printf("  16 voices positive: L %d R %d (expect 32767), mismatches %d\n", res.out[0], res.out[1], res.mismatches);

    for (i = 0; i < 16; ++i)
    {
        ev[i] = (event_t) { 0, i, lo, 127, 127, OUT_RATE };
    }
    res = run(ev, 16, 2, PERIOD);
    ok &= res.mismatches == 0 && res.out[0] == -32768 && res.out[1] == -32768;
    printf("  16 voices negative: L %d R %d (expect -32768), mismatches %d\n", res.out[0], res.out[1], res.mismatches);

    // mixed: 8 positive, 8 negative cancel to -? (-1 * g) per voice pair
    for (i = 0; i < 16; ++i)
    {
        ev[i] = (event_t) { 0, i, (i & 1) ? lo : hi, 127, 127, OUT_RATE };
    }
    res = run(ev, 16, 2, PERIOD);
    ok &= res.mismatches == 0;
    printf("  8 high + 8 low: L %d R %d, mismatches %d\n", res.out[0], res.out[1], res.mismatches);
    report("clamping, full-scale voices", ok, "");
}

static void test_end_mid_period(void)
{
    sfx_t *s = synth("end", OUT_RATE, 700, 2), *s2 = synth("long", OUT_RATE, 2500, 2);
    event_t ev[2];
    result_t res;
    int ok, nz;

    ev[0] = (event_t) { 0, 0, s, 127, 128, OUT_RATE };
    res = run(ev, 1, 3, PERIOD);
    nz = count_nonzero(res.out, 700, 3 * PERIOD);
    ok = res.mismatches == 0 && voices[0].pos == NULL && nz == 0 && count_nonzero(res.out, 0, 700) > 0;
    printf("  700-sample voice at ratio 1: mismatches %d, voice idle %d, nonzero after end %d\n",
           res.mismatches, voices[0].pos == NULL, nz);

    // the voice ends in the middle of a period while another continues
    ev[1] = (event_t) { 0, 1, s2, 90, 128, OUT_RATE };
    res = run(ev, 2, 6, 301);
    ok &= res.mismatches == 0 && voices[0].pos == NULL && voices[1].pos == NULL;
    printf("  plus a 2500-sample voice, last period of 301 frames: mismatches %d, both idle %d\n",
           res.mismatches, voices[0].pos == NULL && voices[1].pos == NULL);

    // a voice whose last sample lands exactly on the period end
    s = synth("exact", OUT_RATE, 2 * PERIOD, 2);
    ev[0] = (event_t) { 0, 0, s, 127, 128, OUT_RATE };
    res = run(ev, 1, 3, PERIOD);
    ok &= res.mismatches == 0 && voices[0].pos == NULL && count_nonzero(res.out, 2 * PERIOD, 3 * PERIOD) == 0;
    printf("  voice ending exactly at a period boundary: mismatches %d, idle %d\n", res.mismatches, voices[0].pos == NULL);
    report("voice end mid-period", ok, "");
}

static void test_start_stop(void)
{
    sfx_t *a = synth("a", 11025, 5000, 2), *b = synth("b", 22050, 4000, 2), *c = synth("c", 8000, 3000, 2);
    // started in period 1, stopped before period 3 (never run past that);
    // started and stopped between two periods with nothing mixed in between
    // (period 4 start and stop both applied before period 4 is mixed);
    // restarted on the same slot while playing.
    event_t ev[] = {
        { 1, 0, a, 127, 100, OUT_RATE },
        { 3, 0, NULL, 0, 0, 0 },
        { 4, 1, b, 127, 128, OUT_RATE },
        { 4, 1, NULL, 0, 0, 0 },
        { 2, 2, c, 100, 200, OUT_RATE },
        { 5, 2, b, 80, 20, OUT_RATE },     // restart of a playing slot
        { 6, 3, a, 127, 128, OUT_RATE },
    };
    result_t res = run(ev, 7, 14, PERIOD);
    int ok = res.mismatches == 0;

    printf("  mismatches %d, hash %08x, frames %d\n", res.mismatches, res.hash, res.frames);
    ok &= count_nonzero(res.out, 0, PERIOD) == 0;
    printf("  period 0 silent: %d\n", count_nonzero(res.out, 0, PERIOD) == 0);
    report("start/stop between periods", ok, "");
}

static void test_steps(void)
{
    static const struct { int in, out; unsigned int step; const char *what; } t[] = {
        { 11025, 22050, 0x8000, "ratio 0.5 (11025 -> 22050)" },
        { 22050, 22050, 0x10000, "ratio 1   (22050 -> 22050)" },
        { 22050, 11025, 0x20000, "ratio 2   (22050 -> 11025)" },
        { 11025, 44100, 0x4000, "ratio 0.25 (11025 -> 44100)" },
        { 8000, 22050, 23777, "odd ratio (8000 -> 22050)" },
        { 11025, 44100, 0x4000, "odd ratio (11025 -> 44100)" },
    };
    sfx_t *ramp = synth("ramp", 0, 20000, 2);
    sfx_t *real = load("DSPISTOL");
    int i, ok = 1;

    for (i = 0; i < 6; ++i)
    {
        sfx_t *s;
        event_t ev[2];
        result_t res;
        unsigned int step = SNDMIX_Step(t[i].in, t[i].out);
        int n = (int) ((((long long) 20000 << 16) / step) / PERIOD) + 2;
        int good;

        ramp->rate = t[i].in;
        s = ramp;
        ev[0] = (event_t) { 0, 0, s, 127, 128, t[i].out };
        res = run(ev, 1, n, PERIOD);
        good = res.mismatches == 0 && step == t[i].step;
        // exact frame count the voice plays: smallest k with (k*step)>>16 >= len
        {
            long long k = 0;
            while (((k * step) >> 16) < 20000) k++;
            // last audible frame index is k-1; frames at and after k are 0
            good &= count_nonzero(res.out, (int) k, res.frames) == 0;
            printf("  %-28s step %u (expect %u) frames played %lld mismatches %d ramp %s\n",
                   t[i].what, step, t[i].step, k, res.mismatches, good ? "ok" : "BAD");
        }
        ok &= good;

        real->rate = t[i].in;
        ev[0] = (event_t) { 0, 0, real, 127, 128, t[i].out };
        res = run(ev, 1, (int) ((((long long) real->len << 16) / step) / PERIOD) + 2, PERIOD);
        ok &= res.mismatches == 0;
        printf("  %-28s on DSPISTOL data: mismatches %d hash %08x\n", "", res.mismatches, res.hash);
    }
    report("16.16 step, ratios", ok, "");
}

// ---------------------------------------------------------------------

static void put32(FILE *f, unsigned int v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void put16(FILE *f, unsigned int v) { fputc(v, f); fputc(v >> 8, f); }

static void test_render(const char *path)
{
    static const struct { double t; const char *n; int vol, sep; } seq[] = {
        { 0.10, "DSPISTOL", 127, 128 }, { 0.40, "DSPISTOL", 127, 128 }, { 0.70, "DSPISTOL", 127, 128 },
        { 1.20, "DSSHOTGN", 127, 150 }, { 1.90, "DSSGCOCK", 100, 150 },
        { 2.30, "DSDOROPN", 90, 60 }, { 2.80, "DSPOSIT1", 80, 20 }, { 3.00, "DSPLPAIN", 127, 128 },
        { 3.40, "DSPOSIT1", 60, 230 }, { 3.55, "DSBAREXP", 127, 128 }, { 3.60, "DSSHOTGN", 127, 128 },
        { 3.65, "DSPISTOL", 127, 0 }, { 3.70, "DSPLPAIN", 127, 254 }, { 3.75, "DSITEMUP", 100, 128 },
        { 3.80, "DSSGCOCK", 127, 128 }, { 3.85, "DSDOROPN", 127, 128 },
        { 5.00, "DSTELEPT", 110, 128 }, { 5.60, "DSSLOP", 100, 128 },
    };
    enum { N = sizeof(seq) / sizeof(seq[0]) };
    event_t ev[N];
    sfx_t *cache[N];
    int i, sec, ok, nsec = 7, periods = nsec * OUT_RATE / PERIOD + 1;
    result_t res;
    FILE *f;
    int frames;

    for (i = 0; i < N; ++i)
    {
        cache[i] = load(seq[i].n);
        // slots: round robin over 8
        ev[i] = (event_t) { (int) (seq[i].t * OUT_RATE / PERIOD), i % 8, cache[i], seq[i].vol, seq[i].sep, OUT_RATE };
    }
    res = run(ev, N, periods, PERIOD);
    frames = res.frames;
    ok = res.mismatches == 0;
    printf("  %d events, %d frames, mismatches %d, hash %08x\n", N, frames, res.mismatches, res.hash);

    f = fopen(path, "wb");
    fwrite("RIFF", 1, 4, f); put32(f, 36 + frames * 4); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, OUT_RATE); put32(f, OUT_RATE * 4); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, frames * 4);
    for (i = 0; i < 2 * frames; ++i)
    {
        put16(f, (uint16_t) res.out[i]);
    }
    fclose(f);

    for (sec = 0; sec < nsec; ++sec)
    {
        double sum = 0;
        int pk = 0, from = sec * OUT_RATE, to = (sec + 1) * OUT_RATE;

        for (i = 2 * from; i < 2 * to && i < 2 * frames; ++i)
        {
            sum += (double) res.out[i] * res.out[i];
            if (abs(res.out[i]) > pk) pk = abs(res.out[i]);
        }
        printf("  second %d: peak %5d (%.1f dBFS) rms %7.1f (%.1f dBFS)\n", sec, pk,
               pk ? 20 * log10(pk / 32768.0) : -999.0, sqrt(sum / (2 * OUT_RATE)),
               sum > 0 ? 10 * log10(sum / (2 * OUT_RATE) / (32768.0 * 32768.0)) : -999.0);
    }
    report("render.wav (bit-exact vs reference)", ok && peak(res.out, 0, frames) > 0, "");
}

// ---------------------------------------------------------------------
// Mono music source (SNDMIX_MixMono). The reference adds in 64 bit with an
// explicit floor division, so it shares no code with the mixer.

static long long floordiv(long long a, long long b)
{
    long long q = a / b;

    if ((a % b != 0) && ((a < 0) != (b < 0)))
    {
        q--;
    }
    return q;
}

static int16_t clamp16(long long x)
{
    return (int16_t) (x > 32767 ? 32767 : x < -32768 ? -32768 : x);
}

static void test_mono_vectors(void)
{
    static const struct { int m, g, acc, out; } t[] = {
        { 32767, 127, 65022, 32511 },
        { -32768, 127, -65024, -32512 },
        { 12345, 0, 0, 0 },
        { -1, 1, -1, -1 },
        { 1, 1, 0, 0 },
        { 32767, 64, 32767, 16383 },
        { -32768, 64, -32768, -16384 },
    };
    int i, ok = 1;

    for (i = 0; i < 7; ++i)
    {
        int16_t src[1] = { (int16_t) t[i].m }, out[2];
        int32_t acc[2] = { 0, 0 }, a0, a1;

        SNDMIX_MixMono(src, t[i].g, acc, 1);
        a0 = acc[0];
        a1 = acc[1];
        ok &= a0 == t[i].acc && a1 == t[i].acc;
        SNDMIX_Output(acc, out, 1);
        ok &= out[0] == t[i].out && out[1] == t[i].out;
        printf("  m %6d gain %3d: acc %6d/%6d (expect %6d) out %6d/%6d (expect %6d)\n",
               t[i].m, t[i].g, a0, a1, t[i].acc, out[0], out[1], t[i].out);
        ok &= acc[0] == 0 && acc[1] == 0;       // Output cleared them
    }
    report("MixMono fixed vectors", ok, "");
}

static unsigned int rng = 12345;

static unsigned int rnd(void)
{
    rng = rng * 1664525u + 1013904223u;
    return rng >> 8;
}

// Sfx voices plus a music source with a random gain every period. The mixer
// is called twice per run: music after the sfx, and music before the sfx.

static void test_mono_random(void)
{
    enum { NP = 400 };
    sfx_t *s[4];
    int16_t *music = malloc(sizeof(int16_t) * PERIOD * NP);
    int gains[NP];
    long long total = (long long) PERIOD * NP;
    long long i;
    unsigned int h_after = 2166136261u, h_before = 2166136261u;
    int mism = 0, order_mism = 0, p, sat = 0;

    s[0] = synth("a", 22050, 60000, 2);
    s[1] = synth("b", 11025, 40000, 2);
    s[2] = synth("c", 22050, 50000, 0);
    s[3] = synth("d", 8000, 30000, 2);
    for (i = 0; i < total; ++i)
    {
        switch ((i / 4096) & 3)
        {
        case 0: music[i] = (int16_t) rnd(); break;              // full-range noise
        case 1: music[i] = (int16_t) (i & 1 ? 32767 : -32768); break;
        case 2: music[i] = (int16_t) ((rnd() & 0x3ff) - 512); break;
        default: music[i] = (int16_t) (i & 0x7fff); break;
        }
    }
    for (p = 0; p < NP; ++p)
    {
        gains[p] = (p % 50 == 0) ? 127 : (p % 50 == 1) ? 0 : (int) (rnd() % 128);
    }

    {
        int32_t acc_a[2 * PERIOD] = { 0 }, acc_b[2 * PERIOD] = { 0 };
        sndvoice_t va[MAXV], vb[MAXV];
        long long f;
        int k;

        memset(va, 0, sizeof(va));
        memset(vb, 0, sizeof(vb));
        for (k = 0; k < 4; ++k)
        {
            va[k].pos = vb[k].pos = s[k]->pcm;
            va[k].end = vb[k].end = s[k]->pcm + s[k]->len;
            va[k].step = vb[k].step = SNDMIX_Step(s[k]->rate, OUT_RATE);
            SNDMIX_SetGains(&va[k], 100 + 5 * k, 20 + 70 * k);
            vb[k].left = va[k].left;
            vb[k].right = va[k].right;
        }
        for (p = 0; p < NP; ++p)
        {
            int16_t oa[2 * PERIOD], ob[2 * PERIOD];

            SNDMIX_Mix(va, MAXV, acc_a, PERIOD);
            SNDMIX_MixMono(music + p * PERIOD, gains[p], acc_a, PERIOD);
            SNDMIX_Output(acc_a, oa, PERIOD);

            SNDMIX_MixMono(music + p * PERIOD, gains[p], acc_b, PERIOD);
            SNDMIX_Mix(vb, MAXV, acc_b, PERIOD);
            SNDMIX_Output(acc_b, ob, PERIOD);

            for (f = 0; f < PERIOD; ++f)
            {
                long long fr = (long long) p * PERIOD + f, al = 0, ar = 0, m;
                int l, r;

                for (k = 0; k < 4; ++k)
                {
                    long long idx = (fr * (((long long) s[k]->rate << 16) / OUT_RATE)) >> 16;
                    int gl = ((254 - (20 + 70 * k)) * (100 + 5 * k)) / 127;
                    int gr = ((20 + 70 * k) * (100 + 5 * k)) / 127;

                    if (idx < s[k]->len)
                    {
                        al += (s[k]->pcm[idx] - 128) * (long long) gl;
                        ar += (s[k]->pcm[idx] - 128) * (long long) gr;
                    }
                }
                m = floordiv((long long) music[fr] * gains[p], 64);
                l = clamp16(floordiv(al + m, 2));
                r = clamp16(floordiv(ar + m, 2));
                sat += l == 32767 || l == -32768;
                mism += oa[2 * f] != l || oa[2 * f + 1] != r;
                order_mism += oa[2 * f] != ob[2 * f] || oa[2 * f + 1] != ob[2 * f + 1];
            }
            h_after = fnv(h_after, oa, sizeof(oa));
            h_before = fnv(h_before, ob, sizeof(ob));
        }
    }
    printf("  %d periods, random gain per period (0 and 127 included), saturated frames %d\n", NP, sat);
    printf("  mismatches vs 64-bit reference: %d; music-before-sfx differs from music-after-sfx: %d\n", mism, order_mism);
    printf("  output FNV-1a (music after) %08x, (music before) %08x\n", h_after, h_before);
    report("MixMono + sfx vs 64-bit reference", mism == 0 && order_mism == 0 && sat > 0 && h_after == h_before, "");
    free(music);
}

// The idle shortcut of the audio thread: a period with no voice and no
// music is not mixed and is written as zeros. 200 periods with voices that
// start and stop at random, once always mixing and once with the shortcut
// (the decision is SNDMIX_IsIdle); the outputs must be identical.

static void test_idle(void)
{
    enum { NP = 200 };
    sfx_t *s[4];
    int32_t acc_a[2 * PERIOD] = { 0 }, acc_b[2 * PERIOD] = { 0 };
    int16_t oa[2 * PERIOD], ob[2 * PERIOD], zeros[2 * PERIOD], music[PERIOD];
    sndvoice_t va[MAXV], vb[MAXV];
    unsigned int h_a = 2166136261u, h_b = 2166136261u;
    int p, k, i, mism = 0, idle_periods = 0, busy_periods = 0, music_periods = 0;

    s[0] = synth("a", 22050, 1500, 2);
    s[1] = synth("b", 11025, 900, 2);
    s[2] = synth("c", 22050, 3000, 0);
    s[3] = synth("d", 8000, 700, 2);
    memset(va, 0, sizeof(va));
    memset(vb, 0, sizeof(vb));
    memset(zeros, 0, sizeof(zeros));
    for (i = 0; i < PERIOD; ++i)
    {
        music[i] = (int16_t) rnd();
    }

    for (p = 0; p < NP; ++p)
    {
        int gain = 0, idle, starts;

        // Mostly quiet: a voice starts in 1 of 6 periods and a running
        // voice may be stopped with a chance of 1 in 16.

        starts = rnd() % 6 == 0;

        for (k = 0; k < MAXV; ++k)
        {
            if (starts && va[k].pos == NULL)
            {
                starts = 0;
                const sfx_t *x = s[rnd() & 3];

                va[k].pos = vb[k].pos = x->pcm;
                va[k].end = vb[k].end = x->pcm + x->len;
                va[k].frac = vb[k].frac = 0;
                va[k].step = vb[k].step = SNDMIX_Step(x->rate, OUT_RATE);
                SNDMIX_SetGains(&va[k], 1 + rnd() % 127, rnd() % 255);
                vb[k].left = va[k].left;
                vb[k].right = va[k].right;
            }
            else if (va[k].pos != NULL && rnd() % 16 == 0)
            {
                va[k].pos = vb[k].pos = NULL;
            }
        }
        if (p % 40 >= 33)
        {
            gain = 1 + rnd() % 127;
            ++music_periods;
        }

        // A: always mix.

        SNDMIX_Mix(va, MAXV, acc_a, PERIOD);
        if (gain > 0)
        {
            SNDMIX_MixMono(music, gain, acc_a, PERIOD);
        }
        SNDMIX_Output(acc_a, oa, PERIOD);

        // B: the shortcut.

        idle = gain <= 0 && SNDMIX_IsIdle(vb, MAXV);
        if (idle)
        {
            memcpy(ob, zeros, sizeof(ob));
            ++idle_periods;
        }
        else
        {
            SNDMIX_Mix(vb, MAXV, acc_b, PERIOD);
            if (gain > 0)
            {
                SNDMIX_MixMono(music, gain, acc_b, PERIOD);
            }
            SNDMIX_Output(acc_b, ob, PERIOD);
            ++busy_periods;
        }

        mism += memcmp(oa, ob, sizeof(oa)) != 0;
        h_a = fnv(h_a, oa, sizeof(oa));
        h_b = fnv(h_b, ob, sizeof(ob));
    }
    printf("  %d periods: %d idle, %d mixed, %d with music; FNV-1a %08x / %08x\n",
           NP, idle_periods, busy_periods, music_periods, h_a, h_b);
    report("idle shortcut equals always-mix", mism == 0 && h_a == h_b
           && idle_periods > 20 && busy_periods > 20, "");
}

int main(int argc, char **argv)
{
    const char *wav = argc > 1 ? argv[1] : "render.wav";

    if (argc > 2)
    {
        wadpath = argv[2];
    }

    printf("sizeof(int)=%zu sizeof(long)=%zu sizeof(void*)=%zu\n", sizeof(int), sizeof(long), sizeof(void *));
    wad_open();
    test_voices(1);
    test_voices(3);
    test_voices(8);
    test_clamp();
    test_end_mid_period();
    test_start_stop();
    test_steps();
    test_mono_vectors();
    test_mono_random();
    test_idle();
    test_render(wav);
    printf("%s\n", nfail ? "SOME TESTS FAILED" : "ALL PASSED");
    return nfail != 0;
}
