// SPDX-License-Identifier: GPL-2.0-or-later
//
// The paletted (C8) present path with a fake compositor and synthetic
// frames: no WAD and no Wayland.
//
//   c8_test
//
// 1. When C8 buffers are chosen, over every combination of what a
//    compositor can announce.
// 2. A C8 buffer seen through its palette equals the XRGB8888 buffer of the
//    old converter, for the geometries of a window, a fullscreen portrait and
//    landscape output and odd sizes; bars and row padding are not written.
// 3. A damage subset converts to the same bytes as a full conversion.
// 4. A palette change at bpp 1 moves no row; at bpp 2 it moves all.
// 5. What a commit needs for a palette change on an exclusive and on a
//    composited surface.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dg_c8.h"
#include "dg_present.h"

static int s_Fails;
static int s_Checks;

#define CHECK(cond, ...) \
	do { \
		s_Checks++; \
		if (!(cond)) { \
			s_Fails++; \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} while (0)

static uint32_t s_Rng = 12345;

static uint32_t rnd(void)
{
	s_Rng = s_Rng * 1664525u + 1013904223u;

	return s_Rng >> 8;
}

// Banded and noisy, so that a wrong row or column shows.
static void makeFrame(uint8_t *src, int seed)
{
	int x, y;

	for (y = 0; y < DGP_SRC_H; y++)
	{
		for (x = 0; x < DGP_SRC_W; x++)
		{
			src[y * DGP_SRC_W + x] = (uint8_t)(x * 3 + y * 7 + seed
			                                   + ((x ^ y) & 1 ? rnd() & 15 : 0));
		}
	}
}

static void makePalette(uint8_t *rgb)
{
	int i;

	for (i = 0; i < 256 * 3; i++)
	{
		rgb[i] = rnd();
	}
}

// 1.
static void testChoice(void)
{
	static const int pres[] = { DGC8_PRES_UNKNOWN, DGC8_PRES_COMPOSITED,
	                            DGC8_PRES_EXCLUSIVE };
	static const uint32_t fmt[] = { 0, DGC8_FORMAT_RGB565, DGC8_FORMAT_C8 };
	int cfg, shm, pal, f, p, wanted = 0;

	CHECK(DGC8_ParseCfg("auto") == DGC8_CFG_AUTO, "auto");
	CHECK(DGC8_ParseCfg("off") == DGC8_CFG_OFF, "off");
	CHECK(DGC8_ParseCfg("on") == -1, "on is not a value");
	CHECK(DGC8_ParseCfg("") == -1, "empty");
	CHECK(DGC8_ParseCfg(NULL) == -1, "null");

	// The fourcc codes are the wl_shm ones: 'C8  ' and 'RG16'.
	CHECK(DGC8_FORMAT_C8 == ('C' | '8' << 8 | ' ' << 16 | ' ' << 24), "C8");
	CHECK(DGC8_FORMAT_RGB565 == ('R' | 'G' << 8 | '1' << 16 | '6' << 24),
	      "RGB565");

	for (cfg = 0; cfg < 2; cfg++)
	for (shm = 0; shm < 2; shm++)
	for (pal = 0; pal < 2; pal++)
	for (f = 0; f < 3; f++)
	for (p = 0; p < 3; p++)
	{
		dgc8_caps_t c = { cfg, shm, pal, fmt[f], pres[p] };
		int expect = cfg == DGC8_CFG_AUTO && shm && pal
		          && fmt[f] == DGC8_FORMAT_C8 && pres[p] == DGC8_PRES_EXCLUSIVE;

		CHECK(DGC8_Wanted(&c) == expect,
		      "cfg=%d shm=%d pal=%d fmt=%x pres=%d", cfg, shm, pal, fmt[f],
		      pres[p]);
		wanted += DGC8_Wanted(&c);
	}
	CHECK(wanted == 1, "exactly one combination chooses C8, got %d", wanted);
}

// A compositor that announces things one at a time, as a real one does.
static void testSequence(void)
{
	dgc8_caps_t c = { DGC8_CFG_AUTO, 1, 0, 0, DGC8_PRES_UNKNOWN };

	CHECK(!DGC8_Wanted(&c), "no palette protocol");
	c.have_palette = 1;
	CHECK(!DGC8_Wanted(&c), "no output announced");
	c.out_format = DGC8_FORMAT_RGB565;
	c.presentation = DGC8_PRES_EXCLUSIVE;
	CHECK(!DGC8_Wanted(&c), "RGB565 scanout");
	c.out_format = DGC8_FORMAT_C8;
	CHECK(DGC8_Wanted(&c), "C8 scanout, exclusive");
	c.presentation = DGC8_PRES_COMPOSITED;
	CHECK(!DGC8_Wanted(&c), "panel appeared");
	c.presentation = DGC8_PRES_EXCLUSIVE;
	CHECK(DGC8_Wanted(&c), "panel hidden");
	c.cfg = DGC8_CFG_OFF;
	CHECK(!DGC8_Wanted(&c), "configured off");
}

typedef struct
{
	int w, h, px, py, pw, ph;
} geo_t;

static const geo_t s_Geo[] =
{
	{ 320, 200, 0, 0, 320, 200 },		// 1:1
	{ 240, 320, 0, 70, 240, 180 },		// fullscreen portrait, 4:3
	{ 320, 240, 0, 0, 320, 240 },		// fullscreen landscape, 4:3
	{ 240, 150, 20, 0, 200, 150 },		// windowed tile
	{ 240, 320, 0, 0, 240, 320 },		// -stretch
	{ 641, 480, 0, 0, 641, 480 },		// odd width: row padding
	{ 243, 181, 1, 2, 241, 175 },		// odd everything
	{ 100, 100, 3, 10, 80, 60 },		// minified
};

#define NGEO (int)(sizeof(s_Geo) / sizeof(s_Geo[0]))

static void setup(dgp_t *c, int mode, const geo_t *g, int bpp,
                  const uint8_t *src)
{
	CHECK(DGP_Init(c, mode, DGP_DAMAGE_ROWS, 4, 0) == 0, "init");
	CHECK(DGP_Resize(c, g->w, g->h, g->px, g->py, g->pw, g->ph, bpp, src) == 0,
	      "resize");
}

// 2 and 3.
static void testBuffers(void)
{
	uint8_t src[DGP_SRC_W * DGP_SRC_H], src2[DGP_SRC_W * DGP_SRC_H];
	uint8_t rgb[256 * 3];
	uint32_t pal[256];
	uint16_t pal565[256];
	int gi, mode;

	makeFrame(src, 1);
	makeFrame(src2, 9);
	makePalette(rgb);
	DGC8_BuildPalette(pal, rgb);
	{
		int i;

		for (i = 0; i < 256; i++)
		{
			pal565[i] = 0;
		}
		CHECK(pal[5] == (((uint32_t)rgb[15] << 16) | (rgb[16] << 8) | rgb[17]),
		      "palette entry 5 is 0x00RRGGBB");
		CHECK((pal[200] >> 24) == 0, "no alpha bits");
	}

	for (mode = DGP_MODE_OLD; mode <= DGP_MODE_NEW; mode++)
	for (gi = 0; gi < NGEO; gi++)
	{
		const geo_t *g = &s_Geo[gi];
		dgp_t c8, rg;
		dgp_plan_t plan;
		size_t n8, n32;
		uint8_t *b8, *b8b, *b32;
		int stride8, x, y, bad = 0;

		setup(&c8, mode, g, 1, src);
		setup(&rg, mode, g, 4, src);
		stride8 = c8.g.stride;
		CHECK(stride8 % 4 == 0 && stride8 >= g->w, "stride %d", stride8);

		n8 = (size_t)stride8 * g->h;
		n32 = (size_t)rg.g.stride * g->h;
		b8 = malloc(n8);
		b8b = malloc(n8);
		b32 = malloc(n32);
		memset(b8, 0xEE, n8);	// sentinel: bars and padding must stay
		memset(b8b, 0xEE, n8);
		memset(b32, 0, n32);

		DGP_Plan(&c8, src, 0, 0, 0, &plan);
		DGP_Convert(&c8, b8, src, pal565, pal, &plan);
		DGP_Plan(&rg, src, 0, 0, 0, &plan);
		DGP_Convert(&rg, b32, src, pal565, pal, &plan);

		for (y = 0; y < g->h; y++)
		{
			for (x = 0; x < g->w; x++)
			{
				int inside = x >= g->px && x < g->px + g->pw
				          && y >= g->py && y < g->py + g->ph;
				uint8_t idx = b8[(size_t)y * stride8 + x];
				uint32_t px = ((uint32_t *)(b32 + (size_t)y * rg.g.stride))[x];

				if (inside)
				{
					bad += pal[idx] != px;
				}
				else
				{
					bad += idx != 0xEE;
				}
			}
			for (x = g->w; x < stride8; x++)
			{
				bad += b8[(size_t)y * stride8 + x] != 0xEE;
			}
		}
		CHECK(bad == 0, "mode %d geo %d: %d wrong pixels", mode, gi, bad);

		// The same frame again as damage only: a second frame with a
		// different middle band, converted by rows into a buffer that
		// holds the first frame.
		memcpy(src2, src, sizeof(src2));
		for (y = 60; y < 90; y++)
		{
			for (x = 0; x < DGP_SRC_W; x++)
			{
				src2[y * DGP_SRC_W + x] ^= 0x5a;
			}
		}
		DGP_Plan(&c8, src2, c8.t.frame, 1, 0, &plan);
		CHECK(!plan.skip && plan.nrects >= 1 && plan.convert, "damage plan");
		memcpy(b8b, b8, n8);
		DGP_Convert(&c8, b8b, src2, pal565, pal, &plan);

		{
			dgp_t full;
			uint8_t *b8c = malloc(n8);

			setup(&full, mode, g, 1, src2);
			memset(b8c, 0xEE, n8);
			DGP_Plan(&full, src2, 0, 0, 0, &plan);
			DGP_Convert(&full, b8c, src2, pal565, pal, &plan);
			CHECK(memcmp(b8b, b8c, n8) == 0,
			      "mode %d geo %d: damage conversion differs from full", mode, gi);
			free(b8c);
			DGP_Free(&full);
		}

		free(b8);
		free(b8b);
		free(b32);
		DGP_Free(&c8);
		DGP_Free(&rg);
	}
}

// 4.
static void testPaletteChange(void)
{
	uint8_t src[DGP_SRC_W * DGP_SRC_H];
	dgp_t c;
	dgp_plan_t plan;
	int bpp;

	makeFrame(src, 3);

	for (bpp = 1; bpp <= 2; bpp++)
	{
		setup(&c, DGP_MODE_NEW, &s_Geo[2], bpp, src);
		DGP_Plan(&c, src, 0, 0, 0, &plan);
		CHECK(plan.whole_buffer, "first draw is the whole buffer");
		DGP_Plan(&c, src, plan.gen, 1, 0, &plan);
		CHECK(plan.skip, "bpp %d: unchanged frame is skipped", bpp);

		DGP_PaletteChanged(&c);
		DGP_Plan(&c, src, c.t.frame, 1, 0, &plan);
		if (bpp == 1)
		{
			CHECK(plan.skip, "bpp 1: a palette change moves no row");
			CHECK(c.st.pal_only == 1 && c.st.pal_full == 0, "counters");
		}
		else
		{
			CHECK(!plan.skip && plan.nrects >= 1,
			      "bpp 2: a palette change moves every row");
			CHECK(c.st.pal_full == 1 && c.st.pal_only == 0, "counters");
		}
		DGP_Free(&c);
	}
}

// 5.
static void testCommitPlan(void)
{
	dgc8_t st;
	dgc8_commit_t out;
	uint32_t a[256], b[256];
	int i;

	for (i = 0; i < 256; i++)
	{
		a[i] = i * 0x010101;
		b[i] = a[i];
	}
	b[7] ^= 0x00ff0000;	// a damage flash tint on one entry

	memset(&st, 0, sizeof(st));
	DGC8_Plan(&st, a, DGC8_PRES_EXCLUSIVE, &out);
	CHECK(out.send && !out.damage_all, "first palette on an exclusive surface");
	DGC8_Plan(&st, a, DGC8_PRES_EXCLUSIVE, &out);
	CHECK(!out.send && !out.damage_all, "same palette");
	DGC8_Plan(&st, b, DGC8_PRES_EXCLUSIVE, &out);
	CHECK(out.send && !out.damage_all, "flash on an exclusive surface: palette only");
	DGC8_Plan(&st, a, DGC8_PRES_COMPOSITED, &out);
	CHECK(out.send && out.damage_all, "change on a composited surface: damage all");
	DGC8_Plan(&st, a, DGC8_PRES_COMPOSITED, &out);
	CHECK(!out.send && !out.damage_all, "composited, same palette");
	DGC8_Reset(&st);
	DGC8_Plan(&st, a, DGC8_PRES_EXCLUSIVE, &out);
	CHECK(out.send, "after a format switch the palette is sent again");
	CHECK(memcmp(st.sent, a, sizeof(a)) == 0, "the sent copy is the palette");
}

int main(void)
{
	testChoice();
	testSequence();
	testBuffers();
	testPaletteChange();
	testCommitPlan();

	printf("C8_TEST: %s (%d checks, %d failures)\n",
	       s_Fails ? "FAIL" : "PASS", s_Checks, s_Fails);

	return s_Fails != 0;
}
