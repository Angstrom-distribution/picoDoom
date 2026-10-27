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

// The status bar covers the last 32 source rows.
#define DGP_SBAR_ROW 168

#define DGP_MAX_RECTS 16

enum
{
	DGP_MODE_OLD,
	DGP_MODE_NEW
};

enum
{
	DGP_DAMAGE_FULL,	// convert every row, damage the whole picture
	DGP_DAMAGE_SBAR,	// compare only the status bar rows
	DGP_DAMAGE_ROWS		// compare every source row the geometry samples
};

// Where the picture sits in a buffer, and how it samples the source.
typedef struct
{
	int width, height;		// the buffer, in pixels
	int picx, picy;			// the picture inside it
	int picw, pich;
	int bpp;			// bytes per buffer pixel: 2 (RGB565) or 4
	int *xmap;			// source column for each output column
	int *ymap;			// source row * DGP_SRC_W for each output row
	uint16_t *yrow;			// source row for each output row
	uint16_t *xoff;			// source column for each output column
	// The smallest period p <= 8 with xoff[i + p] = xoff[i] + q, if any.
	int pat_p, pat_q;
	uint8_t pat_o[8];
	int kernel;			// DGP_K_*, the unrolled kernel that fits
} dgp_geom_t;

enum
{
	DGP_K_GENERIC,
	DGP_K_200,	// width 200: 5 outputs per 8 source columns
	DGP_K_240,	// width 240: 3 outputs per 4 source columns
	DGP_K_320	// width 320: one to one
};

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


// Which source rows changed, and when.  Every compare is one "frame"; a
// source row carries the number of the frame it last changed in.
typedef struct
{
	uint32_t frame;			// the last compare, 1-based
	uint32_t rowgen[DGP_SRC_H];
	uint8_t *prev;			// DGP_SRC_W * DGP_SRC_H history
	int band_lo, band_hi;		// source rows [lo, hi) that are compared
	uint8_t used[DGP_SRC_H];	// some output row samples this source row
} dgp_tracker_t;

typedef struct
{
	uint32_t draws, commits, skipped, full, pal_full;
	uint64_t rows_conv, rows_dmg, bytes_dmg, rects, cmp_rows, cmp_bytes;
} dgp_stats_t;

// Rows y0 to y1 (exclusive) of the picture.
typedef struct
{
	int y0, y1;
} dgp_rect_t;

typedef struct
{
	int skip;			// nothing changed: do not commit
	int whole_buffer;		// damage the entire buffer (first draw)
	int nrects;			// else damage these
	dgp_rect_t rect[DGP_MAX_RECTS];
	const uint8_t *convert;		// output rows to convert, NULL = all
	uint32_t gen;			// store in the buffer after the commit
} dgp_plan_t;

typedef struct
{
	int mode;			// DGP_MODE_*
	int damage;			// DGP_DAMAGE_*
	int maxrects;
	int noskip;
	dgp_geom_t g;
	dgp_tracker_t t;
	uint8_t *convert;		// one flag per output row
	uint8_t *dmgrows;
	int *runlo, *runhi, *hist;	// scratch for DGP_PlanRects
	dgp_stats_t st;
} dgp_t;

// The RGB565 converter.  pic is the first pixel of the picture inside the
// buffer, stride_px the buffer row length in pixels.  It gives the same
// pixels as DGP_ConvertOld.
void DGP_Convert565(uint16_t *pic, int stride_px, const uint8_t *src,
                    const uint16_t *pal, const dgp_geom_t *g,
                    const uint8_t *convert_rows);
const char *DGP_KernelName(const dgp_geom_t *g);

// The tracker.  Rows outside [band_lo, band_hi) always count as changed.
int DGP_TrackerInit(dgp_tracker_t *t, int damage);
void DGP_TrackerFree(dgp_tracker_t *t);
void DGP_TrackerResize(dgp_tracker_t *t, const dgp_geom_t *g,
                       const uint8_t *src);
void DGP_TrackerPalette(dgp_tracker_t *t);
void DGP_TrackerCompare(dgp_tracker_t *t, const uint8_t *src, uint64_t *rows,
                        uint64_t *bytes);
// Fills the per-output-row sets for a buffer that last showed frame gen.
// Returns the number of damaged rows; *nconv gets the converted ones.
int DGP_TrackerSets(const dgp_tracker_t *t, const dgp_geom_t *g, uint32_t gen,
                    int drawn, uint8_t *convert, uint8_t *damage, int *nconv);
// Merges the runs of damaged rows until at most maxrects remain, smallest
// gap first.  lo and hi need (n + 1) / 2 + 1 ints, hist n + 2.
int DGP_PlanRects(const uint8_t *damage, int n, int maxrects, dgp_rect_t *out,
                  int *lo, int *hi, int *hist);

// The per-commit procedure the back end runs.
int DGP_Init(dgp_t *c, int mode, int damage, int maxrects, int noskip);
void DGP_Free(dgp_t *c);
// src is the current frame, or NULL when there is none yet.
int DGP_Resize(dgp_t *c, int width, int height, int picx, int picy, int picw,
               int pich, int bpp, const uint8_t *src);
// The palette really changed.
void DGP_PaletteChanged(dgp_t *c);
// Decides what to convert and what to damage for a buffer that last showed
// frame bufgen (drawn is 0 for a buffer that was never drawn).
void DGP_Plan(dgp_t *c, const uint8_t *src, uint32_t bufgen, int drawn,
              int need_redraw, dgp_plan_t *p);
void DGP_Convert(dgp_t *c, void *data, const uint8_t *src,
                 const uint16_t *pal565, const uint32_t *pal8888,
                 const dgp_plan_t *p);
void DGP_StatsString(const dgp_t *c, char *buf, int size);

#endif
