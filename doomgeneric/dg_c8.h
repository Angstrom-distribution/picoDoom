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
//	The pure part of the paletted (C8) present path: when C8 buffers are
//	used, and what a palette change costs.  It has no Wayland and no engine
//	globals, so the host tests drive it with a fake compositor.
//

#ifndef DG_C8_H
#define DG_C8_H

#include <stdint.h>

// DRM fourcc codes, which are also the wl_shm format codes of these formats.
#define DGC8_FORMAT_C8 0x20203843u
#define DGC8_FORMAT_RGB565 0x36314752u

// The configuration value.
enum
{
	DGC8_CFG_AUTO,	// C8 when the compositor shows it natively
	DGC8_CFG_OFF	// never
};

// What the compositor says about the surface.
enum
{
	DGC8_PRES_UNKNOWN = -1,
	DGC8_PRES_COMPOSITED = 0,	// blended with other surfaces
	DGC8_PRES_EXCLUSIVE = 1		// alone on the output: palette loaded in the display
};

typedef struct
{
	int cfg;		// DGC8_CFG_*
	int shm_c8;		// wl_shm offers C8
	int have_palette;	// the palette protocol is bound for the surface
	uint32_t out_format;	// scanout format of the output, 0 = not announced
	int presentation;	// DGC8_PRES_*
} dgc8_caps_t;

// Parses "auto" or "off".  Returns -1 for anything else.
int DGC8_ParseCfg(const char *s);
// 1 when the surface should draw C8 buffers: C8 in wl_shm, the palette
// protocol, a C8 scanout and an exclusive surface.  Every other case draws
// RGB565 or XRGB8888 as before.
int DGC8_Wanted(const dgc8_caps_t *c);

// The 256 entries as 0x00RRGGBB from r, g, b triplets (the engine's palette
// after its gamma table).
void DGC8_BuildPalette(uint32_t *dst, const uint8_t *rgb);

// The palette last sent for the surface.
typedef struct
{
	uint32_t sent[256];
	int sent_valid;
} dgc8_t;

typedef struct
{
	int send;		// send the palette before this commit
	int damage_all;		// damage the whole buffer in this commit
} dgc8_commit_t;

// Forget what was sent, after a switch of buffer format.
void DGC8_Reset(dgc8_t *c);
// What a commit with palette pal needs.  On an exclusive surface a changed
// palette is only a palette load.  Otherwise the compositor reads the buffer
// through the palette and must see every pixel as changed.
void DGC8_Plan(dgc8_t *c, const uint32_t *pal, int presentation,
               dgc8_commit_t *out);

#endif
