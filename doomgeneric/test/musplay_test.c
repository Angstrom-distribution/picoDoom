// Host test of the music module's code that has no engine or ALSA in it:
// FNV-1a hash and index.txt parser (i_musindex.c), the decoder plug-in
// (i_musdec_flac.c), the playback ring (i_musplay.c) and the mix of sfx and
// music (i_sndmix.c) against an independent 64-bit reference.
//
// Usage: musplay_test <file.flac> <reference.raw> [freedoom1.wad]
// The reference is the signed 16-bit little-endian decode of the FLAC file
// made by `flac -d`. Both are test inputs and are never committed.

#include "../i_musindex.c"
#include "../i_musdec_flac.c"
#include "../i_musplay.c"
#include "../i_sndmix.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int nfail;

static void report(const char *name, int ok)
{
    printf("%-44s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok)
    {
        nfail++;
    }
}

static byte *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    byte *b;

    if (!f)
    {
        fprintf(stderr, "cannot read %s\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc(*len + 1);
    if (fread(b, 1, *len, f) != *len)
    {
        exit(2);
    }
    fclose(f);
    return b;
}

static unsigned int fnv(unsigned int h, const void *d, size_t n)
{
    const unsigned char *p = d;
    while (n--)
    {
        h = (h ^ *p++) * 16777619u;
    }
    return h;
}

// ---------------------------------------------------------------------

static unsigned int rd32(const unsigned char *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (unsigned int) p[3] << 24;
}

static void test_hash_index(const char *wad)
{
    static const char good[] =
        "# comment\n"
        "\n"
        "f9902d79 d_e1m1.flac\n"
        "0ebe8ebc d_e1m2.flac\r\n"
        "f9902d79 duplicate.flac\n"
        "deadbeef ../x\n"
        "deadbeef a/b\n"
        "deadbeef .hidden\n"
        "deadbee d_short.flac\n"
        "deadbeef 0123456789012345678901234567890123456789012345678901234567890123\n"
        "00000001 012345678901234567890123456789012345678901234567890123456789012\n";
    musindex_t idx;
    int ok = 1;
    const char *n;

    ok &= MUSIDX_Hash((const byte *) "", 0) == 0x811c9dc5;
    ok &= MUSIDX_Hash((const byte *) "a", 1) == 0xe40c292c;
    ok &= MUSIDX_Hash((const byte *) "foobar", 6) == 0xbf9cf968;
    printf("  FNV-1a: \"\" %08x, \"a\" %08x, \"foobar\" %08x\n",
           MUSIDX_Hash((const byte *) "", 0), MUSIDX_Hash((const byte *) "a", 1),
           MUSIDX_Hash((const byte *) "foobar", 6));
    report("FNV-1a vectors", ok);

    ok = MUSIDX_Parse(&idx, good, sizeof(good) - 1);
    n = MUSIDX_Lookup(&idx, 0xf9902d79);
    ok &= idx.count == 3 && idx.rejected == 5;
    ok &= n && !strcmp(n, "d_e1m1.flac");
    ok &= (n = MUSIDX_Lookup(&idx, 0x0ebe8ebc)) && !strcmp(n, "d_e1m2.flac");
    ok &= MUSIDX_Lookup(&idx, 0xdeadbeef) == NULL;
    printf("  entries %d (expect 3), rejected %d (expect 5)\n", idx.count, idx.rejected);
    report("index parser", ok);
    MUSIDX_Free(&idx);

    if (wad != NULL)
    {
        size_t len;
        byte *w = slurp(wad, &len);
        int num = rd32(w + 4), i;
        unsigned int dirpos = rd32(w + 8);
        uint32_t h = 0;

        for (i = 0; i < num; ++i)
        {
            const byte *e = w + dirpos + 16 * i;

            if (!strncmp((const char *) e + 8, "D_E1M1", 8))
            {
                h = MUSIDX_Hash(w + rd32(e), rd32(e + 4));
            }
        }
        printf("  D_E1M1 hash %08x (expect f9902d79)\n", h);
        report("D_E1M1 hash from the WAD", h == 0xf9902d79);
        free(w);
    }
}

// ---------------------------------------------------------------------

static byte *flac_data;
static size_t flac_len;
static int16_t *refpcm;
static int nref;

static musplay_t *mp;

static void *open_song(const byte *d, size_t l)
{
    unsigned int rate, ch;
    void *s = musdecoders[0]->Open(d, l, &rate, &ch);

    if (s && (rate != 22050 || ch != 1))
    {
        return NULL;
    }
    return s;
}

// Render `frames` frames in periods of `period`, appending to out.

static int render(int16_t *out, long long frames, int period)
{
    long long done = 0;
    int any = 0;

    while (done < frames)
    {
        int n = frames - done < period ? (int) (frames - done) : period;

        any |= MUSPLAY_Render(mp, out + done, n);
        done += n;
    }
    return any;
}

static long long first_diff(const int16_t *a, const int16_t *b, long long n)
{
    long long i;

    for (i = 0; i < n; ++i)
    {
        if (a[i] != b[i])
        {
            return i;
        }
    }
    return -1;
}

static void test_decode_loop(void)
{
    static const int periods[] = { 256, 333, 512, 1024, 2048 };
    long long total = 3LL * nref + 1000;
    int16_t *out = malloc(total * sizeof(int16_t)), *want = malloc(total * sizeof(int16_t));
    long long i;
    int p, ok = 1;
    void *s = open_song(flac_data, flac_len);

    ok &= s != NULL;
    printf("  dr_flac allocated %u bytes in Open (%u bytes sizeof(musplay_t))\n",
           (unsigned) musdecoders[0]->Bytes(s), (unsigned) sizeof(musplay_t));
    for (i = 0; i < total; ++i)
    {
        want[i] = refpcm[i % nref];
    }
    for (p = 0; p < 5; ++p)
    {
        long long d;

        ok &= MUSPLAY_Start(mp, musdecoders[0], s, true);
        render(out, total, periods[p]);
        d = first_diff(out, want, total);
        printf("  loop, period %4d: %lld frames (3 x %d + 1000), first difference %lld, errors %d\n",
               periods[p], total, nref, d, mp->errors);
        ok &= d < 0 && mp->errors == 0 && MUSPLAY_IsPlaying(mp);
    }
    report("looping equals the reference repeated", ok);
    MUSPLAY_Stop(mp);
    musdecoders[0]->Close(s);
    free(out);
    free(want);
}

// Slices of every size the audio paths use: the periods up to 4096 of
// -audio-period, the 1024 frame slices of the tick mode, the odd amounts a
// fill produces, and a single frame. A fixed size each, then all of them
// mixed in a repeating order. The output must equal the looped reference.

static void test_slices(void)
{
    static const int sizes[] = { 1024, 2048, 4096, 740, 1, 1023 };
    long long total = 3LL * nref + 1000;
    int16_t *out = malloc(total * sizeof(int16_t)), *want = malloc(total * sizeof(int16_t));
    long long i, done, d;
    int p, k = 0, ok = 1, n;
    void *s = open_song(flac_data, flac_len);

    ok &= s != NULL;
    for (i = 0; i < total; ++i)
    {
        want[i] = refpcm[i % nref];
    }
    for (p = 0; p < 6; ++p)
    {
        ok &= MUSPLAY_Start(mp, musdecoders[0], s, true);
        render(out, total, sizes[p]);
        d = first_diff(out, want, total);
        printf("  loop, slices of %4d: first difference %lld, errors %d\n", sizes[p], d, mp->errors);
        ok &= d < 0 && mp->errors == 0;
    }
    ok &= MUSPLAY_Start(mp, musdecoders[0], s, true);
    for (done = 0; done < total; ++k)
    {
        n = sizes[k % 6];
        if (total - done < n)
        {
            n = (int) (total - done);
        }
        MUSPLAY_Render(mp, out + done, n);
        done += n;
    }
    d = first_diff(out, want, total);
    printf("  loop, mixed slices (1024 2048 4096 740 1 1023): %d calls, first difference %lld, errors %d\n",
           k, d, mp->errors);
    ok &= d < 0 && mp->errors == 0 && MUSPLAY_IsPlaying(mp);
    report("slices from 1 to 4096 frames equal the reference", ok);
    MUSPLAY_Stop(mp);
    musdecoders[0]->Close(s);
    free(out);
    free(want);
}

static void test_decode_once(void)
{
    static const int periods[] = { 256, 512, 1000 };
    long long total = (long long) nref + 5000;
    int16_t *out = malloc(total * sizeof(int16_t));
    void *s = open_song(flac_data, flac_len);
    int p, ok = 1;

    for (p = 0; p < 3; ++p)
    {
        long long d, i;
        int period = periods[p], zeros = 1, playing_at_end, k;
        long long periods_to_end = (nref + period - 1) / period;

        ok &= MUSPLAY_Start(mp, musdecoders[0], s, false);
        // period by period, to see when IsPlaying drops
        playing_at_end = 1;
        for (k = 0; (long long) k * period < total; ++k)
        {
            int n = total - (long long) k * period < period ? (int) (total - (long long) k * period) : period;

            MUSPLAY_Render(mp, out + (long long) k * period, n);
            if (k == periods_to_end - 1)
            {
                playing_at_end = MUSPLAY_IsPlaying(mp);
            }
            if (k == periods_to_end)
            {
                ok &= !MUSPLAY_IsPlaying(mp);
            }
        }
        d = first_diff(out, refpcm, nref);
        for (i = nref; i < total; ++i)
        {
            zeros &= out[i] == 0;
        }
        printf("  once, period %4d: first difference %lld, zeros after the end %d, playing after the last period with music %d, now %d\n",
               period, d, zeros, playing_at_end, MUSPLAY_IsPlaying(mp));
        ok &= d < 0 && zeros && mp->errors == 0 && !MUSPLAY_IsPlaying(mp);
    }
    report("not looping: reference, then zeros, then stops", ok);
    musdecoders[0]->Close(s);
    free(out);
}

static void test_pause_stop(void)
{
    enum { P = 512, PAUSED = 7 };
    int16_t *out = malloc((nref + 20000) * sizeof(int16_t));
    void *s = open_song(flac_data, flac_len);
    int ok = 1, k, at = 40, zeros = 1, i;
    long long d;

    // pause for 7 periods after period 40, then resume
    ok &= MUSPLAY_Start(mp, musdecoders[0], s, false);
    for (k = 0; k < at; ++k)
    {
        MUSPLAY_Render(mp, out + k * P, P);
    }
    mp->paused = true;
    for (k = 0; k < PAUSED; ++k)
    {
        ok &= !MUSPLAY_Render(mp, out + (at + k) * P, P);
        ok &= MUSPLAY_IsPlaying(mp);
    }
    mp->paused = false;
    for (k = at + PAUSED; (k - PAUSED) * P < nref + P; ++k)
    {
        MUSPLAY_Render(mp, out + k * P, P);
    }
    d = first_diff(out, refpcm, at * P);
    for (i = 0; i < PAUSED * P; ++i)
    {
        zeros &= out[at * P + i] == 0;
    }
    ok &= d < 0 && zeros;
    ok &= first_diff(out + (at + PAUSED) * P, refpcm + at * P, nref - at * P) < 0;
    printf("  pause of %d periods at period %d: before %lld, zeros %d, after resume %lld\n", PAUSED, at, d, zeros,
           first_diff(out + (at + PAUSED) * P, refpcm + at * P, nref - at * P));
    report("pause keeps the exact position", ok);

    // stop, then start again: frame 0
    ok = MUSPLAY_Start(mp, musdecoders[0], s, true);
    render(out, 5000, 512);
    MUSPLAY_Stop(mp);
    ok &= !MUSPLAY_IsPlaying(mp);
    ok &= !MUSPLAY_Render(mp, out, 512);
    ok &= MUSPLAY_Start(mp, musdecoders[0], s, true);
    render(out, 3000, 512);
    ok &= first_diff(out, refpcm, 3000) < 0;
    report("stop, then start again from frame 0", ok);
    musdecoders[0]->Close(s);
    free(out);
}

static void test_truncated(void)
{
    size_t cut = flac_len * 6 / 10;
    byte *t = malloc(cut);
    int16_t *out = malloc(3LL * nref * sizeof(int16_t));
    void *s;
    int ok = 1, k;

    memcpy(t, flac_data, cut);
    s = open_song(t, cut);
    ok &= s != NULL;
    ok &= MUSPLAY_Start(mp, musdecoders[0], s, true);
    for (k = 0; k < 400; ++k)
    {
        MUSPLAY_Render(mp, out + (k % 50) * 512, 512);
    }
    printf("  truncated at %u of %u bytes: errors %d, ended %d, playing %d\n", (unsigned) cut,
           (unsigned) flac_len, mp->errors, mp->ended, MUSPLAY_IsPlaying(mp));
    ok &= mp->errors > 0 && mp->ended && !MUSPLAY_IsPlaying(mp);
    report("truncated file: error, ended, no crash", ok);
    musdecoders[0]->Close(s);

    // not a FLAC file at all
    ok = open_song((const byte *) "fLaC garbage garbage garbage", 29) == NULL;
    report("garbage after the magic: Open fails", ok);
    free(t);
    free(out);
}

// ---------------------------------------------------------------------
// Full chain: sfx voices + music through MUSPLAY_Render and SNDMIX_MixMono,
// compared with a 64-bit reference that knows only the decoded PCM, the
// event list and the Q7 gain. Loop wrap, pause and the ring boundary all
// happen inside the run (the file is 100001 frames, the ring 8192).

typedef struct
{
    int period;
    int pause;          // 1 pause, 2 resume, 0 none
    int gain;           // -1: unchanged
} mevent_t;

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

static unsigned int rng = 99;

static unsigned int rnd(void)
{
    rng = rng * 1664525u + 1013904223u;
    return rng >> 8;
}

static void test_mix(int period, int nperiods, boolean looping)
{
    static byte sfxdata[3][30000];
    static sndvoice_t voices[3];
    static const int rates[3] = { 22050, 11025, 8000 };
    static const int vols[3] = { 90, 105, 120 }, seps[3] = { 40, 120, 200 };
    long long rstart[3] = { -1, -1, -1 };   // reference: frame each voice started
    long long rstep[3];
    int32_t *acc = calloc(2 * period, sizeof(int32_t));
    int16_t *music = malloc(period * sizeof(int16_t)), *out = malloc(2 * period * sizeof(int16_t));
    void *s = open_song(flac_data, flac_len);
    long long mpos = 0, frame = 0, mism = 0, produced = 0;
    int p, k, i, gain = 64, paused = 0, pauses = 0, ok = 1;
    unsigned int h = 2166136261u;
    char title[96];

    for (k = 0; k < 3; ++k)
    {
        for (i = 0; i < 30000; ++i)
        {
            sfxdata[k][i] = (i * (7 + 4 * k) + (i >> 7)) & 255;
        }
        voices[k].step = SNDMIX_Step(rates[k], 22050);
        voices[k].pos = NULL;
        SNDMIX_SetGains(&voices[k], vols[k], seps[k]);
        rstep[k] = ((long long) rates[k] << 16) / 22050;
    }
    rng = 99 + period;
    ok &= MUSPLAY_Start(mp, musdecoders[0], s, looping);

    for (p = 0; p < nperiods; ++p)
    {
        // events at the start of a period, as the game thread would do them
        if (p % 31 == 5)
        {
            paused = 1;
            mp->paused = true;
            ++pauses;
        }
        if (p % 31 == 9)
        {
            paused = 0;
            mp->paused = false;
        }
        if (p % 13 == 0)
        {
            gain = (p % 52 == 0) ? 127 : (p % 52 == 13) ? 0 : (int) (rnd() % 128);
        }
        if (p % 17 == 3)
        {
            k = p % 3;
            voices[k].pos = sfxdata[k];
            voices[k].end = sfxdata[k] + 30000;
            voices[k].frac = 0;
            rstart[k] = frame;
        }

        // the real chain: sfx, then music, then the clamp
        SNDMIX_Mix(voices, 3, acc, period);
        if (MUSPLAY_Render(mp, music, period))
        {
            SNDMIX_MixMono(music, gain, acc, period);
        }
        SNDMIX_Output(acc, out, period);
        h = fnv(h, out, 2 * period * sizeof(int16_t));

        // the reference: decoded PCM, event list and gain only, 64 bit
        for (i = 0; i < period; ++i)
        {
            long long f = frame + i, al = 0, ar = 0, m = 0;
            int16_t l, r;

            for (k = 0; k < 3; ++k)
            {
                long long idx;

                if (rstart[k] < 0)
                {
                    continue;
                }
                idx = ((f - rstart[k]) * rstep[k]) >> 16;
                if (idx < 30000)
                {
                    al += (sfxdata[k][idx] - 128) * (long long) (((254 - seps[k]) * vols[k]) / 127);
                    ar += (sfxdata[k][idx] - 128) * (long long) ((seps[k] * vols[k]) / 127);
                }
            }
            if (!paused && (looping || mpos < nref))
            {
                m = floordiv((long long) refpcm[mpos % nref] * gain, 64);
                ++produced;
                ++mpos;
            }
            l = clamp16(floordiv(al + m, 2));
            r = clamp16(floordiv(ar + m, 2));
            mism += out[2 * i] != l || out[2 * i + 1] != r;
        }
        frame += period;
    }
    printf("  period %4d, %d periods, %s: %lld music frames (%lld loop wraps), %d pauses, ring read position %u (%u wraps), mismatches %lld, FNV %08x\n",
           period, nperiods, looping ? "loop" : "once", produced, produced / nref, pauses, mp->rd,
           mp->rd / MUSPLAY_RING, mism, h);
    sprintf(title, "sfx + music vs 64-bit reference (%d, %s)", period, looping ? "loop" : "once");
    report(title, ok && mism == 0 && mp->rd / MUSPLAY_RING > 0 && (!looping || produced > nref));
    MUSPLAY_Stop(mp);
    musdecoders[0]->Close(s);
    free(acc);
    free(music);
    free(out);
}

int main(int argc, char **argv)
{
    size_t rawlen;
    byte *raw;

    if (argc < 3)
    {
        fprintf(stderr, "usage: %s file.flac reference.raw [freedoom1.wad]\n", argv[0]);
        return 2;
    }
    printf("sizeof(int)=%zu sizeof(long)=%zu sizeof(void*)=%zu\n", sizeof(int), sizeof(long), sizeof(void *));
    flac_data = slurp(argv[1], &flac_len);
    raw = slurp(argv[2], &rawlen);
    refpcm = (int16_t *) raw;
    nref = rawlen / 2;
    mp = calloc(1, sizeof(*mp));
    printf("FLAC %u bytes, reference %d frames, probe %d\n", (unsigned) flac_len, nref,
           musdecoders[0]->Probe(flac_data, flac_len));

    test_hash_index(argc > 3 ? argv[3] : NULL);
    test_decode_loop();
    test_decode_once();
    test_slices();
    test_pause_stop();
    test_truncated();
    test_mix(512, 1000, true);
    test_mix(333, 1500, true);
    test_mix(1024, 400, true);
    test_mix(512, 600, false);
    printf("%s\n", nfail ? "SOME TESTS FAILED" : "ALL PASSED");
    return nfail != 0;
}
