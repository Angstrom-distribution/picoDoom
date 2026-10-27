// A micro-benchmark of dg_present: the old and the new converter, the change
// compare and the rectangle planner, on real frames from a frame pack or on
// synthetic ones. Static armel for the board; numbers under qemu mean nothing.
//
//   present_bench [-n ITER] [-label board|qemu|host] [frames.pak]
//
// Every figure is milliseconds per call, the mean of ITER calls (default
// 1000), minus the cost of the timer calls. "warm" repeats the call on the
// same data; "cold" evicts the data cache between calls with a 96 KB read, as
// the address-space switches of the real game do (ESTIMATE: the board wipes
// its VIVT caches on every switch, this only approximates that).

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dg_present.h"
#include "pak_read.h"

static double now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);

	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static volatile uint32_t s_Sink;
static uint8_t s_Evict[96 * 1024];

static void evict(void)
{
	uint32_t sum = 0;
	size_t i;

	for (i = 0; i < sizeof(s_Evict); i += 32)
	{
		sum += s_Evict[i];
	}
	s_Sink = sum;
}

typedef struct
{
	const char *name;
	int bw, bh, px, py, pw, ph;
} geo_t;

static const geo_t s_Geo[] =
{
	{ "windowed 240x150 (200x150)", 240, 150, 20, 0, 200, 150 },
	{ "portrait 240x302 (240x180)", 240, 302, 0, 61, 240, 180 },
	{ "landscape 320x240", 320, 240, 0, 0, 320, 240 },
	{ "odd 203x151 (201x151 at x=1)", 203, 151, 1, 0, 201, 151 },
};

static int s_Iter = 1000;
static double s_TimerCost;

typedef void (*job_t)(void *);

static double bench(job_t fn, void *arg, int cold)
{
	double total = 0;
	int i;

	for (i = 0; i < s_Iter; i++)
	{
		double t0;

		if (cold)
		{
			evict();
		}
		t0 = now();
		fn(arg);
		total += now() - t0 - s_TimerCost;
	}

	return total / s_Iter;
}

typedef struct
{
	dgp_geom_t g;
	uint8_t *buf;
	const uint8_t *src[2];
	const uint16_t *pal;
	uint32_t pal32[256];
	const uint32_t *pal8888;
	uint8_t mask[512];
	dgp_tracker_t trk;
	int which;
	uint64_t rows, bytes;
	uint8_t damage[512];
	int lo[512], hi[512], hist[514];
	dgp_rect_t rects[DGP_MAX_RECTS];
} job_data_t;

static void jobOld(void *p)
{
	job_data_t *d = p;

	DGP_ConvertOld(d->buf, &d->g, d->src[0], d->pal, d->pal8888, NULL);
}

static void jobNew(void *p)
{
	job_data_t *d = p;

	DGP_Convert565((uint16_t *)d->buf + (size_t)d->g.picy * (d->g.stride / 2)
	               + d->g.picx, d->g.stride / 2, d->src[0], d->pal, &d->g, NULL);
}

static void jobNew32(void *p)
{
	job_data_t *d = p;

	DGP_Convert565P32((uint16_t *)d->buf + (size_t)d->g.picy * (d->g.stride / 2)
	                  + d->g.picx, d->g.stride / 2, d->src[0], d->pal32, &d->g, NULL);
}

static void jobNew32Mask(void *p)
{
	job_data_t *d = p;

	DGP_Convert565P32((uint16_t *)d->buf + (size_t)d->g.picy * (d->g.stride / 2)
	                  + d->g.picx, d->g.stride / 2, d->src[0], d->pal32, &d->g,
	                  d->mask);
}

static void jobOldMask(void *p)
{
	job_data_t *d = p;

	DGP_ConvertOld(d->buf, &d->g, d->src[0], d->pal, d->pal8888, d->mask);
}

static void jobNewMask(void *p)
{
	job_data_t *d = p;

	DGP_Convert565((uint16_t *)d->buf + (size_t)d->g.picy * (d->g.stride / 2)
	               + d->g.picx, d->g.stride / 2, d->src[0], d->pal, &d->g, d->mask);
}

static void jobCompare(void *p)
{
	job_data_t *d = p;

	DGP_TrackerCompare(&d->trk, d->src[d->which], &d->rows, &d->bytes);
	d->which ^= 1;
}

static void jobRects(void *p)
{
	job_data_t *d = p;

	DGP_PlanRects(d->damage, d->g.pich, 4, d->rects, d->lo, d->hi, d->hist);
}

int main(int argc, char **argv)
{
	const char *label = "unlabelled", *pakpath = NULL;
	static uint8_t synth[2][320 * 200];
	static uint8_t rgb[768];
	uint16_t p565[256];
	uint32_t p8888[256];
	pak_rec_t *recs = NULL;
	long n = 0;
	const uint8_t *frames[2];
	uint32_t x = 2463534242u;
	int i, g;

	for (i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
		{
			s_Iter = atoi(argv[++i]);
		}
		else if (strcmp(argv[i], "-label") == 0 && i + 1 < argc)
		{
			label = argv[++i];
		}
		else
		{
			pakpath = argv[i];
		}
	}

	if (pakpath)
	{
		n = pak_load(pakpath, &recs);
		if (n < 2)
		{
			fprintf(stderr, "cannot read %s\n", pakpath);
			return 2;
		}
		frames[0] = recs[n / 2].px;
		frames[1] = recs[n / 2 + 1].px;
		memcpy(rgb, recs[n / 2].rgb, 768);
		printf("frames: pack %s, records %ld and %ld\n", pakpath, n / 2, n / 2 + 1);
	}
	else
	{
		// Synthetic: 256 colours in runs, the second frame differs in
		// about 80% of the rows.
		for (i = 0; i < 320 * 200; i++)
		{
			x ^= x << 13; x ^= x >> 17; x ^= x << 5;
			synth[0][i] = (i / 7 + (x >> 28)) & 255;
		}
		memcpy(synth[1], synth[0], sizeof(synth[0]));
		for (i = 0; i < 200; i++)
		{
			x ^= x << 13; x ^= x >> 17; x ^= x << 5;
			if ((x & 7) < 6)
			{
				synth[1][i * 320 + ((x >> 8) % 320)] ^= 0x55;
			}
		}
		for (i = 0; i < 768; i++)
		{
			x ^= x << 13; x ^= x >> 17; x ^= x << 5;
			rgb[i] = x >> 24;
		}
		frames[0] = synth[0];
		frames[1] = synth[1];
		printf("frames: synthetic\n");
	}
	for (i = 0; i < 256; i++)
	{
		unsigned r = rgb[i * 3], gg = rgb[i * 3 + 1], b = rgb[i * 3 + 2];

		p565[i] = ((r >> 3) << 11) | ((gg >> 2) << 5) | (b >> 3);
		p8888[i] = (r << 16) | (gg << 8) | b;
	}

	{
		double t0 = now(), t = 0;

		for (i = 0; i < 1000; i++)
		{
			double a = now();

			t += now() - a;
		}
		s_TimerCost = t / 1000;
		(void)t0;
	}

	printf("label: %s, %d iterations, timer cost %.4f ms\n", label, s_Iter,
	       s_TimerCost);
	printf("%-30s %-28s %9s %9s\n", "geometry", "job", "warm ms", "cold ms");

	for (g = 0; g < (int)(sizeof(s_Geo) / sizeof(s_Geo[0])); g++)
	{
		const geo_t *gc = &s_Geo[g];
		job_data_t *d = calloc(1, sizeof(*d));
		uint64_t cmpr = 0, cmpb = 0;
		int y, runs = 0;

		DGP_GeomSet(&d->g, gc->bw, gc->bh, gc->px, gc->py, gc->pw, gc->ph, 2);
		d->buf = calloc(1, (size_t)DGP_Stride(gc->bw, 2) * gc->bh + 64);
		d->src[0] = frames[0];
		d->src[1] = frames[1];
		d->pal = p565;
		d->pal8888 = p8888;
		for (y = 0; y < 256; y++)
		{
			d->pal32[y] = p565[y];
		}
		DGP_TrackerInit(&d->trk, DGP_DAMAGE_ROWS);
		DGP_TrackerResize(&d->trk, &d->g, frames[0]);

		// An 80% row set, as the demo changes.
		for (y = 0; y < gc->ph; y++)
		{
			d->mask[y] = d->g.yrow[y] % 5 != 0;
			if (y == 0 || d->mask[y] != d->mask[y - 1])
			{
				runs++;
			}
		}
		for (y = 0; y < gc->ph; y++)
		{
			d->damage[y] = d->mask[y];
		}
		for (y = 1; y < gc->ph; y++)
		{
			if (d->g.yrow[y] == d->g.yrow[y - 1])
			{
				d->mask[y] = d->mask[y - 1];
			}
		}

		printf("%-30s %-28s %9.4f %9.4f\n", gc->name, "old convert, all rows",
		       bench(jobOld, d, 0), bench(jobOld, d, 1));
		printf("%-30s %-28s %9.4f %9.4f   kernel %s\n", "", "new convert, all rows",
		       bench(jobNew, d, 0), bench(jobNew, d, 1), DGP_KernelName(&d->g));
		printf("%-30s %-28s %9.4f %9.4f\n", "", "new convert, word palette",
		       bench(jobNew32, d, 0), bench(jobNew32, d, 1));
		printf("%-30s %-28s %9.4f %9.4f\n", "", "old convert, 80% rows",
		       bench(jobOldMask, d, 0), bench(jobOldMask, d, 1));
		printf("%-30s %-28s %9.4f %9.4f\n", "", "new convert, 80% rows",
		       bench(jobNewMask, d, 0), bench(jobNewMask, d, 1));
		printf("%-30s %-28s %9.4f %9.4f\n", "", "new, word palette, 80% rows",
		       bench(jobNew32Mask, d, 0), bench(jobNew32Mask, d, 1));
		printf("%-30s %-28s %9.4f %9.4f\n", "", "compare rows (A/B frames)",
		       bench(jobCompare, d, 0), bench(jobCompare, d, 1));
		printf("%-30s %-28s %9.4f %9.4f   (%d runs)\n", "", "plan 4 rectangles",
		       bench(jobRects, d, 0), bench(jobRects, d, 1), runs);
		cmpr = d->rows;
		cmpb = d->bytes;
		printf("%-30s compare read %.1f rows and %.0f bytes per call (all runs)\n",
		       "", (double)cmpr / (2.0 * s_Iter), (double)cmpb / (2.0 * s_Iter));

		DGP_TrackerFree(&d->trk);
		DGP_GeomFree(&d->g);
		free(d->buf);
		free(d);
	}

	free(recs);

	return 0;
}
