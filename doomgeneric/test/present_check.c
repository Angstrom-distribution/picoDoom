// Pixel-exact and damage checks of dg_present, and the damage statistics.
//
//   T1  the RGB565 converter against the old converter, many geometries
//   T2  the damage property with a simulated compositor: 1, 2 and 3 buffers,
//       several release patterns, a resize and palette changes
//   T3  the savings per scaling mode and setting (the same simulations)

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dg_present.h"
#include "present_check.h"

#define MAX_BUFS 3
#define MAX_FAIL_LINES 24
#define RESIZE_AT 3000
#define RESIZE_BACK_AT 5000
#define REDRAW_AT1 4000		// a configure without a size change
#define REDRAW_AT2 6000

// ---------------------------------------------------------------- geometry

typedef struct
{
	const char *name;
	int bw, bh;		// the buffer
	int px, py, pw, ph;	// the picture inside it
	int bpp;
} geo_t;

// The picture fitted into a buffer the way applyResize does it.
static geo_t fit(const char *name, int w, int h, int stretch, int bpp)
{
	geo_t g;
	int pw = w, ph = h;

	if (!stretch)
	{
		ph = (w * 3 + 2) / 4;
		if (ph > h)
		{
			ph = h;
			pw = (h * 4 + 1) / 3;
		}
	}
	if (pw < 1)
	{
		pw = 1;
	}
	if (ph < 1)
	{
		ph = 1;
	}

	g.name = name;
	g.bw = w;
	g.bh = h;
	g.pw = pw;
	g.ph = ph;
	g.px = (w - pw) / 2;
	g.py = (h - ph) / 2;
	g.bpp = bpp;

	return g;
}

static geo_t explicitGeo(const char *name, int bw, int bh, int px, int py,
                         int pw, int ph, int bpp)
{
	geo_t g = { name, bw, bh, px, py, pw, ph, bpp };

	return g;
}

#define MAX_T1 32
static geo_t t1geo[MAX_T1];
static int nt1;
static dgp_geom_t t1g[MAX_T1];

typedef struct
{
	long frames, fails, maskfails;
	long kernel[4];
} t1_stats_t;

static t1_stats_t t1s;

// ------------------------------------------------------------------ common

static int s_Quick;
static long s_Frames;
static long s_PalEvents;
static uint8_t s_PrevRgb[768];
static int s_PrevRgbValid;
static int s_Fails;
static int s_FailLines;
static uint32_t s_Rng = 12345;

static uint32_t rnd(void)
{
	s_Rng = s_Rng * 1664525u + 1013904223u;

	return s_Rng >> 8;
}

static void failLine(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#include <stdarg.h>

static void failLine(const char *fmt, ...)
{
	va_list ap;

	s_Fails++;
	if (s_FailLines >= MAX_FAIL_LINES)
	{
		return;
	}
	s_FailLines++;
	printf("FAIL ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

static void makePalettes(const uint8_t *rgb, uint16_t *p565, uint32_t *p8888)
{
	int i;

	for (i = 0; i < 256; i++)
	{
		unsigned r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];

		p565[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
		p8888[i] = (r << 16) | (g << 8) | b;
	}
}

// Bytes per buffer row: width * bpp rounded up to a multiple of four.
static size_t strideOf(const geo_t *g)
{
	return (size_t)DGP_Stride(g->bw, g->bpp);
}

#define CANARY 0xA5

// Fills a buffer with the canary byte, padding included.
static void fillCanary(uint8_t *p, const geo_t *g)
{
	memset(p, CANARY, strideOf(g) * g->bh);
}

// True when the padding bytes after the last pixel of every row still hold
// the canary: nothing wrote them.
static int paddingIntact(const uint8_t *p, const geo_t *g)
{
	size_t stride = strideOf(g);
	int y;
	size_t i;

	for (y = 0; y < g->bh; y++)
	{
		for (i = (size_t)g->bw * g->bpp; i < stride; i++)
		{
			if (p[y * stride + i] != CANARY)
			{
				return 0;
			}
		}
	}

	return 1;
}

// Compares the picture area of two buffers. Returns the first differing row
// or -1.
static int comparePicture(const uint8_t *a, const uint8_t *b, const geo_t *g)
{
	int y;

	for (y = 0; y < g->ph; y++)
	{
		size_t off = (size_t)(g->py + y) * strideOf(g) + g->px * g->bpp;

		if (memcmp(a + off, b + off, (size_t)g->pw * g->bpp) != 0)
		{
			return y;
		}
	}

	return -1;
}

// The picture area against a reference that does not use the converters or
// their row arithmetic: pixel by pixel from the scaling tables and the
// palette, each pixel addressed from the stride in bytes. Returns the first
// bad row or -1.
static int checkPictureIndependent(const uint8_t *buf, const geo_t *g,
                                   const dgp_geom_t *gg, const uint8_t *src,
                                   const uint16_t *p565, const uint32_t *p8888)
{
	size_t stride = strideOf(g);
	int x, y;

	for (y = 0; y < g->ph; y++)
	{
		const uint8_t *line = src + gg->ymap[y];

		for (x = 0; x < g->pw; x++)
		{
			const uint8_t *px = buf + (size_t)(g->py + y) * stride
			                    + (size_t)(g->px + x) * g->bpp;
			uint8_t idx = line[gg->xmap[x]];

			if (g->bpp == 2)
			{
				uint16_t v;

				memcpy(&v, px, 2);
				if (v != p565[idx])
				{
					return y;
				}
			}
			else
			{
				uint32_t v;

				memcpy(&v, px, 4);
				if (v != p8888[idx])
				{
					return y;
				}
			}
		}
	}

	return -1;
}

// ---------------------------------------------------------------------- T1

static uint8_t *t1a, *t1b;
static dgp_t t1ctx;
static int t1ctxGeo = -1;

static void t1Init(void)
{
	int i;

	t1geo[nt1++] = fit("240x150", 240, 150, 0, 2);
	t1geo[nt1++] = fit("240x180", 240, 180, 0, 2);
	t1geo[nt1++] = fit("320x240", 320, 240, 0, 2);
	t1geo[nt1++] = fit("240x320", 240, 320, 0, 2);
	t1geo[nt1++] = fit("240x302", 240, 302, 0, 2);
	t1geo[nt1++] = fit("160x100", 160, 100, 0, 2);
	t1geo[nt1++] = fit("201x151", 201, 151, 0, 2);
	t1geo[nt1++] = explicitGeo("203x151 odd x", 203, 151, 1, 0, 201, 151, 2);
	t1geo[nt1++] = fit("199x149", 199, 149, 0, 2);
	t1geo[nt1++] = fit("641x481", 641, 481, 0, 2);
	t1geo[nt1++] = fit("1x1", 1, 1, 0, 2);
	t1geo[nt1++] = fit("320x200", 320, 200, 0, 2);
	t1geo[nt1++] = fit("320x200 stretch", 320, 200, 1, 2);
	t1geo[nt1++] = fit("240x150 stretch", 240, 150, 1, 2);
	t1geo[nt1++] = fit("200x150 stretch", 200, 150, 1, 2);
	t1geo[nt1++] = fit("300x100 stretch", 300, 100, 1, 2);
	t1geo[nt1++] = explicitGeo("7x9 odd", 9, 9, 1, 0, 7, 9, 2);
	t1geo[nt1++] = fit("240x150 xrgb", 240, 150, 0, 4);
	// Odd widths in RGB565: the rows are padded to four bytes.
	t1geo[nt1++] = fit("161x120 padded", 161, 120, 0, 2);
	t1geo[nt1++] = fit("159x150 padded", 159, 150, 0, 2);
	t1geo[nt1++] = fit("303x226 padded", 303, 226, 0, 2);
	t1geo[nt1++] = fit("321x240 padded fullscreen", 321, 240, 0, 2);
	t1geo[nt1++] = fit("161x120 stretch padded", 161, 120, 1, 2);
	t1geo[nt1++] = fit("161x120 xrgb", 161, 120, 0, 4);

	for (i = 0; i < nt1; i++)
	{
		if (DGP_GeomSet(&t1g[i], t1geo[i].bw, t1geo[i].bh, t1geo[i].px,
		                t1geo[i].py, t1geo[i].pw, t1geo[i].ph,
		                t1geo[i].bpp) < 0)
		{
			printf("out of memory\n");
			exit(2);
		}
	}

	t1a = malloc(1024 * 1024);
	t1b = malloc(1024 * 1024);
	if (!t1a || !t1b)
	{
		exit(2);
	}
}

static void t1Frame(const uint8_t *src, const uint16_t *p565,
                    const uint32_t *p8888, int frameno)
{
	int i;
	int step = s_Quick ? 1 : 1;

	(void)step;
	for (i = 0; i < nt1; i++)
	{
		const geo_t *g = &t1geo[i];
		size_t stride = strideOf(g);
		size_t size = stride * g->bh;
		int bad;

		fillCanary(t1a, g);
		fillCanary(t1b, g);

		DGP_ConvertOld(t1a, &t1g[i], src, p565, p8888, NULL);
		if (g->bpp == 2)
		{
			DGP_Convert565((uint16_t *)t1b + g->py * (stride / 2) + g->px,
			               (int)(stride / 2), src, p565, &t1g[i], NULL);
		}
		else
		{
			// The dispatcher: the new mode falls back to the old converter.
			dgp_plan_t plan;

			if (t1ctxGeo != i)
			{
				if (t1ctxGeo >= 0)
				{
					DGP_Free(&t1ctx);
				}
				DGP_Init(&t1ctx, DGP_MODE_NEW, DGP_DAMAGE_FULL, 4, 0);
				DGP_Resize(&t1ctx, g->bw, g->bh, g->px, g->py, g->pw, g->ph,
				           g->bpp, src);
				t1ctxGeo = i;
			}
			DGP_Plan(&t1ctx, src, 0, 1, 0, &plan);
			DGP_Convert(&t1ctx, t1b, src, p565, p8888, &plan);
		}

		bad = comparePicture(t1a, t1b, g);
		if (bad >= 0)
		{
			t1s.fails++;
			failLine("T1 frame %d geometry %s: row %d differs", frameno,
			         g->name, bad);
		}
		bad = checkPictureIndependent(t1b, g, &t1g[i], src, p565, p8888);
		if (bad >= 0)
		{
			t1s.fails++;
			failLine("T1 frame %d geometry %s: row %d differs from the "
			         "table reference", frameno, g->name, bad);
		}
		if (!paddingIntact(t1a, g) || !paddingIntact(t1b, g))
		{
			t1s.fails++;
			failLine("T1 frame %d geometry %s: row padding was written",
			         frameno, g->name);
		}

		// The word palette variant.
		if (g->bpp == 2)
		{
			uint32_t pal32[256];
			int k;

			for (k = 0; k < 256; k++)
			{
				pal32[k] = p565[k];
			}
			fillCanary(t1b, g);
			DGP_Convert565P32((uint16_t *)t1b + g->py * (stride / 2) + g->px,
			                  (int)(stride / 2), src, pal32, &t1g[i], NULL);
			if (!paddingIntact(t1b, g))
			{
				t1s.fails++;
				failLine("T1 frame %d geometry %s: word palette wrote padding",
				         frameno, g->name);
			}
			bad = comparePicture(t1a, t1b, g);
			if (bad >= 0)
			{
				t1s.fails++;
				failLine("T1 frame %d geometry %s: word palette row %d differs",
				         frameno, g->name, bad);
			}
		}
		t1s.kernel[t1g[i].kernel]++;

		// The same with a random set of rows, over the whole buffer: the
		// rows that are not converted must keep what was there.
		if (g->bpp == 2 && (i == 0 || i == 6 || i == 7 || i == 9
		 || stride != (size_t)g->bw * g->bpp))
		{
			uint8_t mask[1024];
			uint8_t flag[DGP_SRC_H];
			int y;

			for (y = 0; y < DGP_SRC_H; y++)
			{
				flag[y] = (rnd() & 3) != 0;
			}
			for (y = 0; y < g->ph; y++)
			{
				mask[y] = flag[t1g[i].yrow[y]];
			}

			fillCanary(t1a, g);
			fillCanary(t1b, g);
			DGP_ConvertOld(t1a, &t1g[i], src, p565, p8888, mask);
			DGP_Convert565((uint16_t *)t1b + g->py * (stride / 2) + g->px,
			               (int)(stride / 2), src, p565, &t1g[i], mask);
			if (memcmp(t1a, t1b, size) != 0)
			{
				t1s.maskfails++;
				failLine("T1 masked frame %d geometry %s differs", frameno,
				         g->name);
			}
		}
	}

	t1s.frames++;
}

// ------------------------------------------------------------- simulations

enum
{
	P_ONE,		// K=1: released before every draw
	P_ALT,		// K=2: strict alternation
	P_ZERO,		// K=2: always buffer 0
	P_LCG2,		// K=2: random release, frames without a free buffer
	P_LCG3		// K=3: the same
};

static const char *patName[] = { "k1", "k2alt", "k2zero", "k2lcg", "k3lcg" };

typedef struct
{
	char name[80];
	int damage, rects, pattern, mode;
	int nbuf;
	int resize;			// forced resizes at RESIZE_AT, RESIZE_BACK_AT
	int geo[2];			// indexes into simGeo; [1] is the resize target
	int cur;			// 0 or 1
	int checks;			// compare against the reference
	// state
	dgp_t ctx;
	uint8_t *buf[MAX_BUFS];
	uint32_t gen[MAX_BUFS];
	int drawn[MAX_BUFS];
	uint8_t *screen, *ref;
	size_t size;
	uint16_t pal565[256];
	uint32_t pal8888[256];
	int palValid;
	int alt;
	int needRedraw;
	uint32_t lcg;
	uint32_t seed;
	long attempts, palEvents, resizes, fails;
} sim_t;

#define MAX_SIMS 80
static sim_t *sims[MAX_SIMS];
static int nsims;

static geo_t simGeo[8];
static int nsimGeo;

static uint32_t simRnd(sim_t *s)
{
	s->lcg = s->lcg * 1103515245u + 12345u;

	return s->lcg >> 8;
}

static void simBuffers(sim_t *s)
{
	const geo_t *g = &simGeo[s->geo[s->cur]];
	int i;

	s->size = strideOf(g) * g->bh;
	for (i = 0; i < MAX_BUFS; i++)
	{
		s->buf[i] = realloc(s->buf[i], s->size);
		memset(s->buf[i], CANARY, s->size);
		s->gen[i] = 0;
		s->drawn[i] = 0;
	}
	s->screen = realloc(s->screen, s->size);
	s->ref = realloc(s->ref, s->size);
	memset(s->screen, CANARY, s->size);
	memset(s->ref, CANARY, s->size);
}

static void simResize(sim_t *s, const uint8_t *src)
{
	const geo_t *g = &simGeo[s->geo[s->cur]];

	if (DGP_Resize(&s->ctx, g->bw, g->bh, g->px, g->py, g->pw, g->ph, g->bpp,
	               src) < 0)
	{
		printf("out of memory\n");
		exit(2);
	}
	simBuffers(s);
	s->needRedraw = 1;
}

static sim_t *simNew(const char *tag, int geo, int geo2, int damage, int rects,
                     int pattern, int mode, int resize)
{
	sim_t *s = calloc(1, sizeof(*s));

	snprintf(s->name, sizeof(s->name), "%s %s %s rects=%d %s%s", tag,
	         simGeo[geo].name,
	         damage == DGP_DAMAGE_FULL ? "full" :
	         damage == DGP_DAMAGE_SBAR ? "sbar" : "rows", rects,
	         patName[pattern], mode == DGP_MODE_NEW ? " new" : " old");
	s->damage = damage;
	s->rects = rects;
	s->pattern = pattern;
	s->mode = mode;
	s->nbuf = pattern == P_ONE ? 1 : pattern == P_LCG3 ? 3 : 2;
	s->resize = resize;
	s->geo[0] = geo;
	s->geo[1] = geo2;
	// T2 varies the release sequence; T3 uses one, so the settings compare
	s->lcg = strcmp(tag, "T3") == 0 ? 0x1234567u : 0x1234567u + nsims * 7919u;
	s->seed = s->lcg;
	s->checks = 1;
	if (DGP_Init(&s->ctx, mode, damage, rects, 0) < 0)
	{
		exit(2);
	}
	sims[nsims++] = s;

	return s;
}

static void simShow(const sim_t *s, int frame, const char *what, const uint8_t *a,
                    const uint8_t *b)
{
	const geo_t *g = &simGeo[s->geo[s->cur]];
	size_t i;
	int row;

	for (i = 0; i < s->size && a[i] == b[i]; i++)
	{
	}
	row = (int)(i / strideOf(g));
	failLine("T2 %s frame %d: %s differs, first at buffer row %d (picture row %d)",
	         s->name, frame, what, row, row - g->py);
}

static void simFrame(sim_t *s, const uint8_t *src, const uint8_t *rgb,
                     int frameno)
{
	const geo_t *g;
	uint16_t p565[256];
	uint32_t p8888[256];
	dgp_plan_t plan;
	int pick = 0;
	int i;

	if (s->resize && (frameno == RESIZE_AT || frameno == RESIZE_BACK_AT))
	{
		s->cur = frameno == RESIZE_AT ? 1 : 0;
		s->resizes++;
		simResize(s, src);
	}
	if (s->resize && (frameno == REDRAW_AT1 || frameno == REDRAW_AT2))
	{
		s->needRedraw = 1;
	}
	if (s->cur == 0 && s->attempts == 0)
	{
		simResize(s, src);
	}
	g = &simGeo[s->geo[s->cur]];

	s->attempts++;

	switch (s->pattern)
	{
	case P_ONE:
	case P_ZERO:
		pick = 0;
		break;
	case P_ALT:
		pick = s->alt;
		break;
	default:
	{
		uint32_t mask, r = simRnd(s);
		int n, k;

		if ((r & 7) == 0)
		{
			return;		// the frame callback is still pending
		}
		mask = simRnd(s) & ((1u << s->nbuf) - 1);
		if (!mask)
		{
			return;		// no buffer is free
		}
		n = 0;
		for (i = 0; i < s->nbuf; i++)
		{
			n += (mask >> i) & 1;
		}
		k = simRnd(s) % n;
		for (i = 0; i < s->nbuf; i++)
		{
			if ((mask >> i) & 1)
			{
				if (k-- == 0)
				{
					pick = i;
					break;
				}
			}
		}
		break;
	}
	}

	makePalettes(rgb, p565, p8888);
	if (s->palValid && (memcmp(p565, s->pal565, sizeof(p565)) != 0
	 || memcmp(p8888, s->pal8888, sizeof(p8888)) != 0))
	{
		DGP_PaletteChanged(&s->ctx);
		s->palEvents++;
	}
	memcpy(s->pal565, p565, sizeof(p565));
	memcpy(s->pal8888, p8888, sizeof(p8888));
	s->palValid = 1;

	DGP_Plan(&s->ctx, src, s->gen[pick], s->drawn[pick], s->needRedraw, &plan);

	// A redraw request must always commit, with the whole picture damaged.
	if (s->needRedraw && s->drawn[pick]
	 && (plan.skip || plan.whole_buffer || plan.nrects != 1
	  || plan.rect[0].y0 != 0 || plan.rect[0].y1 != g->ph))
	{
		s->fails++;
		failLine("T2 %s frame %d: redraw request not honoured", s->name,
		         frameno);
	}

	if (s->checks)
	{
		// The full conversion of the current frame.
		DGP_ConvertOld(s->ref, &s->ctx.g, src, p565, p8888, NULL);
	}

	if (plan.skip)
	{
		if (s->checks && memcmp(s->screen, s->ref, s->size) != 0)
		{
			s->fails++;
			simShow(s, frameno, "screen after a skipped commit", s->screen,
			        s->ref);
		}
		return;
	}

	DGP_Convert(&s->ctx, s->buf[pick], src, p565, p8888, &plan);

	// The compositor takes only what was damaged.
	if (plan.whole_buffer)
	{
		memcpy(s->screen, s->buf[pick], s->size);
	}
	else
	{
		for (i = 0; i < plan.nrects; i++)
		{
			int y;

			if (plan.rect[i].y0 < 0 || plan.rect[i].y1 > g->ph
			 || plan.rect[i].y0 >= plan.rect[i].y1)
			{
				s->fails++;
				failLine("T2 %s frame %d: bad rectangle %d..%d", s->name,
				         frameno, plan.rect[i].y0, plan.rect[i].y1);
				continue;
			}
			for (y = plan.rect[i].y0; y < plan.rect[i].y1; y++)
			{
				size_t off = (size_t)(g->py + y) * strideOf(g)
				             + g->px * g->bpp;

				memcpy(s->screen + off, s->buf[pick] + off,
				       (size_t)g->pw * g->bpp);
			}
		}
	}

	if (s->checks)
	{
		if (memcmp(s->buf[pick], s->ref, s->size) != 0)
		{
			s->fails++;
			simShow(s, frameno, "committed buffer", s->buf[pick], s->ref);
		}
		if (memcmp(s->screen, s->ref, s->size) != 0)
		{
			s->fails++;
			simShow(s, frameno, "screen", s->screen, s->ref);
		}
	}

	s->drawn[pick] = 1;
	s->gen[pick] = plan.gen;
	s->needRedraw = 0;
	if (s->pattern == P_ALT)
	{
		s->alt ^= 1;
	}
}

static void simsInit(void)
{
	int gi, d, p;
	int damages[2] = { DGP_DAMAGE_SBAR, DGP_DAMAGE_ROWS };
	int pats[5] = { P_ONE, P_ALT, P_ZERO, P_LCG2, P_LCG3 };
	int rects[3] = { 1, 4, 16 };
	int r;
	int dm[3] = { DGP_DAMAGE_FULL, DGP_DAMAGE_SBAR, DGP_DAMAGE_ROWS };

	simGeo[nsimGeo++] = fit("240x150", 240, 150, 0, 2);	// 0 windowed
	simGeo[nsimGeo++] = fit("240x180", 240, 180, 0, 2);	// 1
	simGeo[nsimGeo++] = fit("201x151", 201, 151, 0, 2);	// 2 odd
	simGeo[nsimGeo++] = fit("240x302", 240, 302, 0, 2);	// 3 portrait
	simGeo[nsimGeo++] = fit("320x240", 320, 240, 0, 2);	// 4 landscape
	simGeo[nsimGeo++] = explicitGeo("203x151", 203, 151, 1, 0, 201, 151, 2);
	simGeo[nsimGeo++] = fit("161x120", 161, 120, 0, 2);	// 6 padded rows
	simGeo[nsimGeo++] = fit("303x226", 303, 226, 0, 2);	// 7 padded rows

	// T2: the damage property
	for (gi = 0; gi < 3; gi++)
	{
		int next = (gi + 1) % 3;

		for (d = 0; d < 2; d++)
		{
			for (p = 0; p < 5; p++)
			{
				if (s_Quick && (p == P_ZERO || p == P_ONE || gi == 1))
				{
					continue;
				}
				simNew("T2", gi, gi == 2 ? 5 : next, damages[d], 4, pats[p],
				       DGP_MODE_NEW, 1);
			}
		}
	}
	// Odd widths: padded rows, with a resize between two padded sizes. The
	// canary in the padding must survive in every buffer and on the screen.
	for (d = 0; d < 2; d++)
	{
		for (p = 0; p < 5; p++)
		{
			simNew("T2", 6, 7, damages[d], 4, pats[p], DGP_MODE_NEW, 1);
		}
	}
	simNew("T2", 6, 7, DGP_DAMAGE_ROWS, 4, P_ALT, DGP_MODE_OLD, 1);
	// The old converter with the same planner.
	simNew("T2", 0, 1, DGP_DAMAGE_ROWS, 4, P_ALT, DGP_MODE_OLD, 1);
	simNew("T2", 0, 1, DGP_DAMAGE_SBAR, 4, P_LCG2, DGP_MODE_OLD, 1);

	// T3: the savings. Windowed, portrait and landscape; alternating
	// buffers and random release.
	for (gi = 0; gi < 3; gi++)
	{
		int geo = gi == 0 ? 0 : gi == 1 ? 3 : 4;

		for (d = 0; d < 3; d++)
		{
			for (p = 0; p < 2; p++)
			{
				simNew("T3", geo, geo, dm[d], 4, p ? P_LCG2 : P_ALT,
				       DGP_MODE_NEW, 0);
			}
		}
	}
	for (d = 1; d < 3; d++)
	{
		for (r = 0; r < 3; r++)
		{
			if (rects[r] == 4)
			{
				continue;
			}
			for (p = 0; p < 2; p++)
			{
				simNew("T3", 0, 0, dm[d], rects[r], p ? P_LCG2 : P_ALT,
				       DGP_MODE_NEW, 0);
			}
		}
	}
}

// -------------------------------------------------------------- unit cases

static uint8_t s_Frame1[PC_FRAME_BYTES];
static int s_HaveFrame1;

// Cases the demo does not hit on purpose: an unchanged frame, a redraw
// request, -damage-noskip, and the number of rows each mode compares.
static void unitCases(void)
{
	const geo_t g = fit("240x150", 240, 150, 0, 2);
	int modes[2] = { DGP_DAMAGE_SBAR, DGP_DAMAGE_ROWS };
	int m, y, noskip;

	if (!s_HaveFrame1)
	{
		return;
	}

	for (m = 0; m < 2; m++)
	{
		for (noskip = 0; noskip < 2; noskip++)
		{
			dgp_t c;
			dgp_plan_t p;
			uint8_t seen[DGP_SRC_H] = { 0 };
			int lo = modes[m] == DGP_DAMAGE_SBAR ? DGP_SBAR_ROW : 0;
			int expect = 0, r;
			uint32_t gen;
			const char *name = modes[m] == DGP_DAMAGE_SBAR ? "sbar" : "rows";

			DGP_Init(&c, DGP_MODE_NEW, modes[m], 4, noskip);
			DGP_Resize(&c, g.bw, g.bh, g.px, g.py, g.pw, g.ph, g.bpp,
			           s_Frame1);

			for (y = 0; y < g.ph; y++)
			{
				seen[((2 * y + 1) * DGP_SRC_H) / (2 * g.ph)] = 1;
			}
			for (r = lo; r < DGP_SRC_H; r++)
			{
				expect += seen[r];
			}

			DGP_Plan(&c, s_Frame1, 0, 0, 0, &p);
			if (p.skip || !p.whole_buffer)
			{
				failLine("unit %s: the first draw of a buffer must damage all of it", name);
			}
			if (c.st.cmp_rows != (uint64_t)expect)
			{
				failLine("unit %s: compared %llu rows, expected %d", name,
				         (unsigned long long)c.st.cmp_rows, expect);
			}
			gen = p.gen;

			// The same frame again.
			DGP_Plan(&c, s_Frame1, gen, 1, 0, &p);
			if (modes[m] == DGP_DAMAGE_ROWS && !noskip)
			{
				if (!p.skip)
				{
					failLine("unit rows: an unchanged frame was not skipped");
				}
			}
			else if (modes[m] == DGP_DAMAGE_ROWS && noskip)
			{
				if (p.skip || p.nrects != 0 || p.whole_buffer)
				{
					failLine("unit rows noskip: expected a commit without damage");
				}
				gen = p.gen;
			}
			else if (p.skip)
			{
				failLine("unit sbar: skipped although rows outside the bar count as changed");
			}
			else
			{
				gen = p.gen;
			}

			// A redraw request is never skipped and damages the picture.
			DGP_Plan(&c, s_Frame1, gen, 1, 1, &p);
			if (p.skip || p.whole_buffer || p.nrects != 1 || p.rect[0].y0 != 0
			 || p.rect[0].y1 != g.ph)
			{
				failLine("unit %s: a redraw request must damage the whole picture", name);
			}

			DGP_Free(&c);
		}
	}
}

// The stride rule: width * bpp rounded up to four bytes, and unchanged when
// that is already a multiple of four, so the even widths keep their layout.
static void strideCases(void)
{
	int w, bpp;

	for (bpp = 2; bpp <= 4; bpp += 2)
	{
		for (w = 1; w <= 1300; w++)
		{
			int s = DGP_Stride(w, bpp);
			dgp_geom_t g;

			if (s % 4 != 0 || s < w * bpp || s - w * bpp > 3
			 || ((w * bpp) % 4 == 0 && s != w * bpp))
			{
				failLine("unit stride: width %d bpp %d gave %d", w, bpp, s);
				continue;
			}
			memset(&g, 0, sizeof(g));
			if (DGP_GeomSet(&g, w, 2, 0, 0, w, 2, bpp) < 0 || g.stride != s)
			{
				failLine("unit stride: DGP_GeomSet stride %d, expected %d for "
				         "width %d bpp %d", g.stride, s, w, bpp);
			}
			DGP_GeomFree(&g);
		}
	}
	// The sizes the board uses: nothing changes for them.
	if (DGP_Stride(320, 2) != 640 || DGP_Stride(240, 2) != 480
	 || DGP_Stride(201, 4) != 804 || DGP_Stride(161, 2) != 324)
	{
		failLine("unit stride: known sizes");
	}
}

// ----------------------------------------------------------------- public

void PC_Init(int quick)
{
	s_Quick = quick;
	t1Init();
	simsInit();
}

void PC_Frame(const uint8_t *src, const uint8_t *rgb, int frameno)
{
	uint16_t p565[256];
	uint32_t p8888[256];
	int i;

	makePalettes(rgb, p565, p8888);
	if (s_PrevRgbValid && memcmp(rgb, s_PrevRgb, 768) != 0)
	{
		s_PalEvents++;
	}
	memcpy(s_PrevRgb, rgb, 768);
	s_PrevRgbValid = 1;

	if (!s_HaveFrame1)
	{
		memcpy(s_Frame1, src, PC_FRAME_BYTES);
		s_HaveFrame1 = 1;
	}

	t1Frame(src, p565, p8888, frameno);

	for (i = 0; i < nsims; i++)
	{
		simFrame(sims[i], src, rgb, frameno);
	}

	s_Frames++;
}

static double pct(uint64_t v, uint64_t base)
{
	return base ? 100.0 * (double)v / (double)base : 0.0;
}

int PC_Report(void)
{
	int i, t2fails = 0;
	long t2commits = 0, t2skips = 0, t2pal = 0, t2sims = 0;

	printf("\n== T1 pixel-exact old vs new\n");
	printf("T1: %ld frames, %d geometries, %ld mismatching frames, %ld masked mismatches\n",
	       t1s.frames, nt1, t1s.fails, t1s.maskfails);
	printf("T1: kernels used per frame-geometry: generic=%ld k200=%ld k240=%ld k320=%ld\n",
	       t1s.kernel[DGP_K_GENERIC], t1s.kernel[DGP_K_200],
	       t1s.kernel[DGP_K_240], t1s.kernel[DGP_K_320]);
	for (i = 0; i < nt1; i++)
	{
		printf("T1:   %-16s picture %dx%d at %d,%d bpp %d kernel %s pattern p=%d q=%d\n",
		       t1geo[i].name, t1geo[i].pw, t1geo[i].ph, t1geo[i].px,
		       t1geo[i].py, t1geo[i].bpp, DGP_KernelName(&t1g[i]),
		       t1g[i].pat_p, t1g[i].pat_q);
	}

	printf("\n== T2 damage property (screen model)\n");
	for (i = 0; i < nsims; i++)
	{
		const sim_t *s = sims[i];

		if (strncmp(s->name, "T2", 2) != 0)
		{
			continue;
		}
		t2sims++;
		t2fails += s->fails;
		t2commits += s->ctx.st.commits;
		t2skips += s->ctx.st.skipped;
		t2pal += s->palEvents;
		printf("T2: %-48s seed=%08x attempts=%ld commits=%u skipped=%u resizes=%ld palette=%ld fails=%ld\n",
		       s->name, s->seed, s->attempts, s->ctx.st.commits,
		       s->ctx.st.skipped, s->resizes, s->palEvents, s->fails);
	}
	printf("T2: %ld simulations, %ld commits, %ld skipped, %ld palette events in the frames (%ld in the simulations), %d failures\n",
	       t2sims, t2commits, t2skips, s_PalEvents, t2pal, t2fails);
	if (s_PalEvents == 0)
	{
		failLine("T2 no palette change in the demo");
	}

	{
		int before = s_Fails;

		unitCases();
		printf("unit cases (unchanged frame, redraw request, -damage-noskip, rows compared): %s\n",
		       s_Fails == before ? "passed" : "FAILED");
	}

	{
		int before = s_Fails;

		strideCases();
		printf("unit cases (row stride: multiple of four, unchanged for even widths): %s\n",
		       s_Fails == before ? "passed" : "FAILED");
	}

	printf("\n== T3 savings (bytes damaged, relative to the full setting of the same scaling mode and release pattern)\n");
	for (i = 0; i < nsims; i++)
	{
		const sim_t *s = sims[i];
		const sim_t *base = NULL;
		int j;

		if (strncmp(s->name, "T3", 2) != 0)
		{
			continue;
		}
		for (j = 0; j < nsims; j++)
		{
			if (strncmp(sims[j]->name, "T3", 2) == 0
			 && sims[j]->damage == DGP_DAMAGE_FULL
			 && sims[j]->geo[0] == s->geo[0] && sims[j]->pattern == s->pattern)
			{
				base = sims[j];
			}
		}
		printf("T3: %-44s draws=%u commits=%u skipped=%u rows_conv=%" PRIu64
		       " rows_dmg=%" PRIu64 " bytes_dmg=%" PRIu64 " rects=%" PRIu64
		       " cmp_rows=%" PRIu64 " cmp_bytes=%" PRIu64
		       " | bytes %.1f%% rows_conv %.1f%% commits %.1f%%\n",
		       s->name, s->ctx.st.draws, s->ctx.st.commits, s->ctx.st.skipped,
		       s->ctx.st.rows_conv, s->ctx.st.rows_dmg, s->ctx.st.bytes_dmg,
		       s->ctx.st.rects, s->ctx.st.cmp_rows, s->ctx.st.cmp_bytes,
		       base ? pct(s->ctx.st.bytes_dmg, base->ctx.st.bytes_dmg) : 0.0,
		       base ? pct(s->ctx.st.rows_conv, base->ctx.st.rows_conv) : 0.0,
		       base ? pct(s->ctx.st.commits, base->ctx.st.commits) : 0.0);
	}

	printf("\n== result: %ld frames, T1 failures %ld, T2/T3 simulation failures %d, other %d\n",
	       s_Frames, t1s.fails + t1s.maskfails, t2fails, s_Fails);
	for (i = 0; i < nsims; i++)
	{
		if (sims[i]->fails && strncmp(sims[i]->name, "T3", 2) == 0)
		{
			t2fails += sims[i]->fails;
			printf("T3 simulation %s: %ld failures\n", sims[i]->name,
			       sims[i]->fails);
		}
	}

	return s_Fails + t1s.fails + t1s.maskfails;
}
