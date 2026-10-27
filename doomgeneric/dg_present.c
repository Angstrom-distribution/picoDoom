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

#include <stdlib.h>
#include <string.h>

#include "dg_present.h"

void DGP_GeomFree(dgp_geom_t *g)
{
	free(g->xmap);
	free(g->ymap);
	memset(g, 0, sizeof(*g));
}

int DGP_GeomSet(dgp_geom_t *g, int width, int height, int picx, int picy,
                int picw, int pich, int bpp)
{
	int *xmap, *ymap;
	int i;

	g->width = width;
	g->height = height;
	g->picx = picx;
	g->picy = picy;
	g->picw = picw;
	g->pich = pich;
	g->bpp = bpp;

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
	if (!xmap || !ymap)
	{
		return -1;
	}

	// Sample the source at the centre of each output pixel.
	for (i = 0; i < picw; i++)
	{
		g->xmap[i] = ((2 * i + 1) * DGP_SRC_W) / (2 * picw);
	}
	for (i = 0; i < pich; i++)
	{
		g->ymap[i] = ((2 * i + 1) * DGP_SRC_H) / (2 * pich) * DGP_SRC_W;
	}

	return 0;
}

void DGP_ConvertOld(void *data, const dgp_geom_t *g, const uint8_t *src,
                    const uint16_t *pal565, const uint32_t *pal8888,
                    const uint8_t *convert_rows)
{
	int stride = g->width * g->bpp;
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
