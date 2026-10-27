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
//	The pure part of the Wayland presentation path, see dg_present.h.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dg_present.h"

// 32-bit access to byte and halfword arrays.
typedef uint32_t __attribute__((__may_alias__)) dgp_u32;

void DGP_GeomFree(dgp_geom_t *g)
{
	free(g->xmap);
	free(g->ymap);
	free(g->yrow);
	free(g->xoff);
	memset(g, 0, sizeof(*g));
}

static const uint8_t s_Pat200[5] = { 0, 2, 4, 5, 7 };
static const uint8_t s_Pat240[3] = { 0, 2, 3 };

// Finds the repeating pattern of the source columns and the unrolled kernel
// that matches it. The pattern is checked against every output column, so a
// kernel is only used when it gives exactly the table's columns.
static void findPattern(dgp_geom_t *g)
{
	int p, i;

	g->pat_p = g->pat_q = 0;
	g->kernel = DGP_K_GENERIC;

	for (p = 1; p <= 8 && 2 * p <= g->picw; p++)
	{
		int q = g->xoff[p] - g->xoff[0];

		for (i = 0; i + p < g->picw; i++)
		{
			if (g->xoff[i + p] != g->xoff[i] + q)
			{
				break;
			}
		}
		if (i + p >= g->picw)
		{
			g->pat_p = p;
			g->pat_q = q;
			for (i = 0; i < p; i++)
			{
				g->pat_o[i] = g->xoff[i];
			}
			break;
		}
	}

	if (g->pat_p == 5 && g->pat_q == 8 && memcmp(g->pat_o, s_Pat200, 5) == 0)
	{
		g->kernel = DGP_K_200;
	}
	else if (g->pat_p == 3 && g->pat_q == 4
	      && memcmp(g->pat_o, s_Pat240, 3) == 0)
	{
		g->kernel = DGP_K_240;
	}
	else if (g->pat_p == 1 && g->pat_q == 1 && g->pat_o[0] == 0)
	{
		g->kernel = DGP_K_320;
	}
}

int DGP_Stride(int width, int bpp)
{
	return (width * bpp + 3) & ~3;
}

int DGP_GeomSet(dgp_geom_t *g, int width, int height, int picx, int picy,
                int picw, int pich, int bpp)
{
	int *xmap, *ymap;
	uint16_t *yrow, *xoff;
	int i;

	g->width = width;
	g->height = height;
	g->picx = picx;
	g->picy = picy;
	g->picw = picw;
	g->pich = pich;
	g->bpp = bpp;
	g->stride = DGP_Stride(width, bpp);

	xmap = realloc(g->xmap, picw * sizeof(*xmap));
	if (xmap)
	{
		g->xmap = xmap;
	}
	ymap = realloc(g->ymap, pich * sizeof(*ymap));
	if (ymap)
	{
		g->ymap = ymap;
	}
	yrow = realloc(g->yrow, pich * sizeof(*yrow));
	if (yrow)
	{
		g->yrow = yrow;
	}
	xoff = realloc(g->xoff, picw * sizeof(*xoff));
	if (xoff)
	{
		g->xoff = xoff;
	}
	if (!xmap || !ymap || !yrow || !xoff)
	{
		return -1;
	}

	// Sample the source at the centre of each output pixel.
	for (i = 0; i < picw; i++)
	{
		g->xmap[i] = ((2 * i + 1) * DGP_SRC_W) / (2 * picw);
		g->xoff[i] = g->xmap[i];
	}
	for (i = 0; i < pich; i++)
	{
		g->yrow[i] = ((2 * i + 1) * DGP_SRC_H) / (2 * pich);
		g->ymap[i] = g->yrow[i] * DGP_SRC_W;
	}

	findPattern(g);

	return 0;
}

const char *DGP_KernelName(const dgp_geom_t *g)
{
	switch (g->kernel)
	{
	case DGP_K_200:
		return "200";
	case DGP_K_240:
		return "240";
	case DGP_K_320:
		return "320";
	}

	return "generic";
}

void DGP_ConvertOld(void *data, const dgp_geom_t *g, const uint8_t *src,
                    const uint16_t *pal565, const uint32_t *pal8888,
                    const uint8_t *convert_rows)
{
	int stride = g->stride;
	int x, y;

	for (y = 0; y < g->pich; y++)
	{
		char *row = (char *)data + (size_t)(g->picy + y) * stride
		            + g->picx * g->bpp;
		const uint8_t *line = src + g->ymap[y];

		if (convert_rows && !convert_rows[y])
		{
			continue;
		}

		if (y > 0 && g->ymap[y] == g->ymap[y - 1])
		{
			memcpy(row, row - stride, g->picw * g->bpp);
		}
		else if (g->bpp == 2)
		{
			uint16_t *dst = (uint16_t *)row;

			for (x = 0; x < g->picw; x++)
			{
				dst[x] = pal565[line[g->xmap[x]]];
			}
		}
		else
		{
			uint32_t *dst = (uint32_t *)row;

			for (x = 0; x < g->picw; x++)
			{
				dst[x] = pal8888[line[g->xmap[x]]];
			}
		}
	}
}


// Two output pixels per 32-bit store. All the source bytes of a group are
// read before the first store, because a store through a halfword or word
// pointer may alias the byte source as far as the compiler knows.
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define PAIR(a, b) (((uint32_t)pal[a] << 16) | pal[b])
#else
#define PAIR(a, b) ((uint32_t)pal[a] | ((uint32_t)pal[b] << 16))
#endif

#define DGP_CAT2(a, b) a##b
#define DGP_CAT(a, b) DGP_CAT2(a, b)

#define DGP_PALT uint16_t
#define DGP_SUFFIX p16
#define DGP_CONVERT DGP_Convert565
#include "dg_present_conv.inc"
#undef DGP_PALT
#undef DGP_SUFFIX
#undef DGP_CONVERT

#define DGP_PALT uint32_t
#define DGP_SUFFIX p32
#define DGP_CONVERT DGP_Convert565P32
#include "dg_present_conv.inc"
#undef DGP_PALT
#undef DGP_SUFFIX
#undef DGP_CONVERT

#define PASS_WORDS 8

int DGP_TrackerInit(dgp_tracker_t *t, int damage)
{
	memset(t, 0, sizeof(*t));

	t->band_lo = damage == DGP_DAMAGE_SBAR ? DGP_SBAR_ROW : 0;
	t->band_hi = DGP_SRC_H;

	if (damage == DGP_DAMAGE_FULL)
	{
		return 0;
	}

	t->prev = calloc(1, DGP_SRC_W * DGP_SRC_H);

	return t->prev ? 0 : -1;
}

void DGP_TrackerFree(dgp_tracker_t *t)
{
	free(t->prev);
	t->prev = NULL;
}

// The buffers are recreated after a resize, so every row counts as changed
// for all of them, and the history starts again from the current frame.
void DGP_TrackerResize(dgp_tracker_t *t, const dgp_geom_t *g,
                       const uint8_t *src)
{
	int r, y;

	memset(t->used, 0, sizeof(t->used));
	for (y = 0; y < g->pich; y++)
	{
		t->used[g->yrow[y]] = 1;
	}

	for (r = 0; r < DGP_SRC_H; r++)
	{
		t->rowgen[r] = t->frame + 1;
	}

	if (t->prev && src)
	{
		memcpy(t->prev, src, DGP_SRC_W * DGP_SRC_H);
	}
}

void DGP_TrackerPalette(dgp_tracker_t *t)
{
	int r;

	for (r = 0; r < DGP_SRC_H; r++)
	{
		t->rowgen[r] = t->frame + 1;
	}
}

void DGP_TrackerCompare(dgp_tracker_t *t, const uint8_t *src, uint64_t *rows,
                        uint64_t *bytes)
{
	uint32_t frame = ++t->frame;
	uint64_t nrows = 0, nbytes = 0;
	int r;

	for (r = 0; r < DGP_SRC_H; r++)
	{
		const dgp_u32 *a, *b;
		int i, changed = 0;

		if (r < t->band_lo || r >= t->band_hi)
		{
			t->rowgen[r] = frame;
			continue;
		}
		if (!t->used[r])
		{
			continue;
		}

		a = (const dgp_u32 *)(src + r * DGP_SRC_W);
		b = (const dgp_u32 *)(t->prev + r * DGP_SRC_W);

		__builtin_prefetch(src + (r + 1) * DGP_SRC_W);
		__builtin_prefetch(t->prev + (r + 1) * DGP_SRC_W);

		nrows++;
		for (i = 0; i < DGP_SRC_W / 4; i += PASS_WORDS)
		{
			uint32_t d = (a[i] ^ b[i]) | (a[i + 1] ^ b[i + 1])
			           | (a[i + 2] ^ b[i + 2]) | (a[i + 3] ^ b[i + 3])
			           | (a[i + 4] ^ b[i + 4]) | (a[i + 5] ^ b[i + 5])
			           | (a[i + 6] ^ b[i + 6]) | (a[i + 7] ^ b[i + 7]);

			if (d)
			{
				// Keep the history current from here to the end of the row.
				nbytes += (i + PASS_WORDS) * 4;
				memcpy((uint8_t *)b + i * 4, (const uint8_t *)a + i * 4,
				       DGP_SRC_W - i * 4);
				changed = 1;
				break;
			}
		}
		if (changed)
		{
			t->rowgen[r] = frame;
		}
		else
		{
			nbytes += DGP_SRC_W;
		}
	}

	*rows += nrows;
	*bytes += nbytes;
}

int DGP_TrackerSets(const dgp_tracker_t *t, const dgp_geom_t *g, uint32_t gen,
                    int drawn, uint8_t *convert, uint8_t *damage, int *nconv)
{
	int ndmg = 0, nc = 0;
	int y;

	for (y = 0; y < g->pich; y++)
	{
		uint32_t rg = t->rowgen[g->yrow[y]];

		// A buffer that was never drawn has nothing right in it.
		convert[y] = !drawn || rg > gen;
		damage[y] = !drawn || rg == t->frame;
		nc += convert[y];
		ndmg += damage[y];
	}

	*nconv = nc;

	return ndmg;
}

int DGP_PlanRects(const uint8_t *damage, int n, int maxrects, dgp_rect_t *out,
                  int *lo, int *hi, int *hist)
{
	int nruns = 0;
	int ties = 0, thresh = 0;
	int nout = 0;
	int y, i;

	if (maxrects > DGP_MAX_RECTS)
	{
		maxrects = DGP_MAX_RECTS;
	}
	if (maxrects < 1)
	{
		maxrects = 1;
	}

	for (y = 0; y < n;)
	{
		if (!damage[y])
		{
			y++;
			continue;
		}
		lo[nruns] = y;
		while (y < n && damage[y])
		{
			y++;
		}
		hi[nruns++] = y;
	}

	if (nruns > maxrects)
	{
		// The gaps are independent of each other, so merging the smallest
		// ones first is merging the (nruns - maxrects) smallest.
		int merges = nruns - maxrects;
		int below = 0;

		memset(hist, 0, (n + 2) * sizeof(*hist));
		for (i = 1; i < nruns; i++)
		{
			hist[lo[i] - hi[i - 1]]++;
		}
		for (thresh = 1; thresh <= n; thresh++)
		{
			if (below + hist[thresh] >= merges)
			{
				break;
			}
			below += hist[thresh];
		}
		ties = merges - below;
	}

	for (i = 0; i < nruns; i++)
	{
		if (nout > 0 && i > 0)
		{
			int gap = lo[i] - hi[i - 1];

			if (nruns > maxrects
			 && (gap < thresh || (gap == thresh && ties > 0)))
			{
				if (gap == thresh)
				{
					ties--;
				}
				out[nout - 1].y1 = hi[i];
				continue;
			}
		}
		out[nout].y0 = lo[i];
		out[nout].y1 = hi[i];
		nout++;
	}

	return nout;
}

int DGP_Init(dgp_t *c, int mode, int damage, int maxrects, int noskip)
{
	memset(c, 0, sizeof(*c));
	c->mode = mode;
	c->damage = damage;
	c->maxrects = maxrects;
	c->noskip = noskip;

	return DGP_TrackerInit(&c->t, damage);
}

void DGP_Free(dgp_t *c)
{
	DGP_GeomFree(&c->g);
	DGP_TrackerFree(&c->t);
	free(c->convert);
	free(c->dmgrows);
	free(c->runlo);
	free(c->runhi);
	free(c->hist);
	c->convert = c->dmgrows = NULL;
	c->runlo = c->runhi = c->hist = NULL;
}

int DGP_Resize(dgp_t *c, int width, int height, int picx, int picy, int picw,
               int pich, int bpp, const uint8_t *src)
{
	int runs = (pich + 1) / 2 + 1;
	void *p;

	if (DGP_GeomSet(&c->g, width, height, picx, picy, picw, pich, bpp) < 0)
	{
		return -1;
	}

	p = realloc(c->convert, pich);
	if (!p)
	{
		return -1;
	}
	c->convert = p;
	p = realloc(c->dmgrows, pich);
	if (!p)
	{
		return -1;
	}
	c->dmgrows = p;
	p = realloc(c->runlo, runs * sizeof(int));
	if (!p)
	{
		return -1;
	}
	c->runlo = p;
	p = realloc(c->runhi, runs * sizeof(int));
	if (!p)
	{
		return -1;
	}
	c->runhi = p;
	p = realloc(c->hist, (pich + 2) * sizeof(int));
	if (!p)
	{
		return -1;
	}
	c->hist = p;

	DGP_TrackerResize(&c->t, &c->g, src);

	return 0;
}

void DGP_PaletteChanged(dgp_t *c)
{
	DGP_TrackerPalette(&c->t);
	c->st.pal_full++;
}

void DGP_Plan(dgp_t *c, const uint8_t *src, uint32_t bufgen, int drawn,
              int need_redraw, dgp_plan_t *p)
{
	dgp_stats_t *st = &c->st;
	int pich = c->g.pich;
	int ndmg, nconv, i;

	memset(p, 0, sizeof(*p));
	st->draws++;

	if (c->damage == DGP_DAMAGE_FULL)
	{
		// Today's behaviour: every row, the whole picture.
		c->t.frame++;
		p->gen = c->t.frame;
		nconv = pich;
		p->nrects = 1;
		p->rect[0].y0 = 0;
		p->rect[0].y1 = pich;
	}
	else
	{
		DGP_TrackerCompare(&c->t, src, &st->cmp_rows, &st->cmp_bytes);
		ndmg = DGP_TrackerSets(&c->t, &c->g, bufgen, drawn, c->convert,
		                       c->dmgrows, &nconv);

		if (drawn && ndmg == 0 && !need_redraw && !c->noskip)
		{
			// The compare left the history equal to this frame and no
			// row carries this number: take it back.
			c->t.frame--;
			st->skipped++;
			p->skip = 1;
			return;
		}

		p->gen = c->t.frame;
		p->convert = drawn ? c->convert : NULL;
		if (!drawn)
		{
			nconv = pich;
		}
		else if (need_redraw)
		{
			p->nrects = 1;
			p->rect[0].y0 = 0;
			p->rect[0].y1 = pich;
		}
		else
		{
			p->nrects = DGP_PlanRects(c->dmgrows, pich, c->maxrects,
			                          p->rect, c->runlo, c->runhi, c->hist);
		}
	}

	if (!drawn)
	{
		p->whole_buffer = 1;
		p->nrects = 0;
		st->full++;
		st->bytes_dmg += (uint64_t)c->g.stride * c->g.height;
	}
	else
	{
		uint64_t rows = 0;

		for (i = 0; i < p->nrects; i++)
		{
			rows += p->rect[i].y1 - p->rect[i].y0;
		}
		st->rows_dmg += rows;
		st->bytes_dmg += rows * c->g.picw * c->g.bpp;
		st->rects += p->nrects;
	}

	st->commits++;
	st->rows_conv += nconv;
}

void DGP_Convert(dgp_t *c, void *data, const uint8_t *src,
                 const uint16_t *pal565, const uint32_t *pal8888,
                 const dgp_plan_t *p)
{
	if (c->mode == DGP_MODE_NEW && c->g.bpp == 2)
	{
		DGP_Convert565((uint16_t *)data + (size_t)c->g.picy * (c->g.stride / 2)
		               + c->g.picx, c->g.stride / 2, src, pal565, &c->g,
		               p->convert);
	}
	else
	{
		DGP_ConvertOld(data, &c->g, src, pal565, pal8888, p->convert);
	}
}

void DGP_StatsString(const dgp_t *c, char *buf, int size)
{
	const dgp_stats_t *s = &c->st;

	snprintf(buf, size, "DG_PresentStats: draws=%u commits=%u skipped=%u "
	         "full=%u rows_conv=%llu rows_dmg=%llu bytes_dmg=%llu "
	         "rects=%llu cmp_rows=%llu cmp_bytes=%llu pal_full=%u",
	         s->draws, s->commits, s->skipped, s->full,
	         (unsigned long long)s->rows_conv,
	         (unsigned long long)s->rows_dmg,
	         (unsigned long long)s->bytes_dmg,
	         (unsigned long long)s->rects,
	         (unsigned long long)s->cmp_rows,
	         (unsigned long long)s->cmp_bytes, s->pal_full);
}
