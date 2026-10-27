// Host test of the tick fill rule (i_alsatick.c): every row of the table
// in the design, with and without deep, then an exhaustive sweep of the
// invariants.

#include "../i_alsatick.c"

#include <stdio.h>
#include <string.h>

static int nfail;

static void check(const char *name, const tickcfg_t *cfg, long delay,
                  int deep, int started, unsigned int forward,
                  unsigned int write, int underrun)
{
    tickplan_t p;
    int ok;

    memset(&p, 0xaa, sizeof(p));
    TICK_Plan(cfg, delay, deep, started, &p);
    ok = p.forward == forward && p.write == write && p.underrun == underrun;
    printf("%-44s delay %6ld deep %d started %d: forward %5u write %5u underrun %d %s\n",
           name, delay, deep, started, p.forward, p.write, p.underrun,
           ok ? "PASS" : "FAIL");
    nfail += !ok;
}

int main(void)
{
    // 22050 Hz, 1024 x 8, D = 100 ms = 2205 frames.
    tickcfg_t c = { 2205, 8192, 1024, 1024 };
    long d;
    unsigned long checked = 0;
    int deep, started, bad = 0;

    check("ran dry by 5000 frames", &c, -5000, 0, 1, 5000, 2205, 1);
    check("ran dry by 5000 frames, deep", &c, -5000, 1, 1, 5000, 7168, 1);
    check("ran dry by 1 frame", &c, -1, 0, 1, 1, 2205, 1);
    check("empty, stream started", &c, 0, 0, 1, 0, 2205, 1);
    check("empty, stream started, deep", &c, 0, 1, 1, 0, 7168, 1);
    check("empty, stream not started", &c, 0, 0, 0, 0, 2205, 0);
    check("empty, stream not started, deep", &c, 0, 1, 0, 0, 7168, 0);
    check("one frame queued", &c, 1, 0, 1, 0, 2204, 0);
    check("D - 1 queued", &c, 2204, 0, 1, 0, 1, 0);
    check("D queued", &c, 2205, 0, 1, 0, 0, 0);
    check("D + 1 queued", &c, 2206, 0, 1, 0, 0, 0);
    check("D - 1 queued, deep", &c, 2204, 1, 1, 0, 7168 - 2204, 0);
    check("D queued, deep", &c, 2205, 1, 1, 0, 7168 - 2205, 0);
    check("buffer - period queued, deep", &c, 7168, 1, 1, 0, 0, 0);
    check("buffer - 1 queued", &c, 8191, 0, 1, 0, 0, 0);
    check("buffer queued", &c, 8192, 0, 1, 0, 0, 0);
    check("buffer queued, deep", &c, 8192, 1, 1, 0, 0, 0);
    check("more than the buffer (bad value)", &c, 100000, 0, 1, 0, 0, 0);

    // A buffer of exactly 2 D: a deep fill still leaves a period.
    c.depth = 4096;
    c.buffer = 8192;
    check("D = buffer / 2, empty", &c, 0, 0, 1, 0, 4096, 1);
    check("D = buffer / 2, deep, delay 100", &c, 100, 1, 1, 0, 7068, 0);

    // Sweep: for every delay from -9000 to 9000, every combination.
    {
        const tickcfg_t cfgs[] = {
            { 2205, 8192, 1024, 1024 }, { 441, 4096, 1024, 1024 },
            { 2205, 4608, 512, 1024 }, { 22050, 65536, 4096, 1024 },
            { 1102, 2205 * 2, 64, 1024 }, { 4096, 8192, 2048, 1024 },
        };
        unsigned int i;

        for (i = 0; i < sizeof(cfgs) / sizeof(cfgs[0]); ++i)
        {
            for (d = -(long) cfgs[i].buffer - 1000;
                 d <= (long) cfgs[i].buffer + 1000; ++d)
            {
                for (deep = 0; deep < 2; ++deep)
                {
                    for (started = 0; started < 2; ++started)
                    {
                        tickplan_t p;
                        long q = d < 0 ? 0 : d;
                        unsigned int target = deep ? cfgs[i].buffer - cfgs[i].period
                                                   : cfgs[i].depth;
                        int under = d < 0 || (d == 0 && started);

                        TICK_Plan(&cfgs[i], d, deep, started, &p);
                        ++checked;

                        // never writes past the end of the buffer
                        if (p.write > cfgs[i].buffer - (q > (long) cfgs[i].buffer ? cfgs[i].buffer : q))
                        {
                            bad++;
                        }
                        // forward only after an underrun, and exactly the dry frames
                        if (p.forward != (d < 0 ? (unsigned int) -d : 0))
                        {
                            bad++;
                        }
                        if (p.underrun != under)
                        {
                            bad++;
                        }
                        // after the fill the queue holds the target (if it fits)
                        if (!under && q <= (long) cfgs[i].buffer
                            && (unsigned int) q < target
                            && (unsigned int) q + p.write != target)
                        {
                            bad++;
                        }
                        if (under && p.write != target)
                        {
                            bad++;
                        }
                        // at or above the target nothing is written
                        if (!under && (unsigned int) q >= target && p.write != 0)
                        {
                            bad++;
                        }
                    }
                }
            }
        }
    }
    printf("sweep: %lu cases, %d violations %s\n", checked, bad, bad ? "FAIL" : "PASS");
    nfail += bad != 0;
    printf("%s\n", nfail ? "SOME TESTS FAILED" : "ALL PASSED");
    return nfail != 0;
}
