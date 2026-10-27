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
//	The pure part of the Wayland presentation path: the scaling tables and
//	the converters from the 320x200 indexed frame to the shm buffer.
//	It has no Wayland and no engine globals, only the arguments it is given,
//	so the host tests link the same object the game runs.
//

#ifndef DG_PRESENT_H
#define DG_PRESENT_H

#include <stdint.h>

#define DGP_SRC_W 320
#define DGP_SRC_H 200

// Where the picture sits in a buffer, and how it samples the source.
typedef struct
{
	int width, height;		// the buffer, in pixels
	int picx, picy;			// the picture inside it
	int picw, pich;
	int bpp;			// bytes per buffer pixel: 2 (RGB565) or 4
	int *xmap;			// source column for each output column
	int *ymap;			// source row * DGP_SRC_W for each output row
} dgp_geom_t;

// Sets the geometry and builds the scaling tables: each output pixel
// samples the source at its centre.  Returns -1 when out of memory.
int DGP_GeomSet(dgp_geom_t *g, int width, int height, int picx, int picy,
                int picw, int pich, int bpp);
void DGP_GeomFree(dgp_geom_t *g);

// The converter the back end has always used.  data is the buffer base.
// convert_rows is NULL for every row, or one flag per output row; the rows
// of one source row must carry the same flag.
void DGP_ConvertOld(void *data, const dgp_geom_t *g, const uint8_t *src,
                    const uint16_t *pal565, const uint32_t *pal8888,
                    const uint8_t *convert_rows);

#endif
