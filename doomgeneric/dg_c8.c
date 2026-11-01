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
//	The pure part of the paletted (C8) present path, see dg_c8.h.
//

#include <string.h>

#include "dg_c8.h"

int DGC8_ParseCfg(const char *s)
{
	if (s && strcmp(s, "auto") == 0)
	{
		return DGC8_CFG_AUTO;
	}
	if (s && strcmp(s, "off") == 0)
	{
		return DGC8_CFG_OFF;
	}

	return -1;
}

int DGC8_Wanted(const dgc8_caps_t *c)
{
	return c->cfg == DGC8_CFG_AUTO && c->shm_c8 && c->have_palette
	    && c->out_format == DGC8_FORMAT_C8
	    && c->presentation == DGC8_PRES_EXCLUSIVE;
}

void DGC8_BuildPalette(uint32_t *dst, const uint8_t *rgb)
{
	int i;

	for (i = 0; i < 256; i++)
	{
		dst[i] = ((uint32_t)rgb[3 * i] << 16) | ((uint32_t)rgb[3 * i + 1] << 8)
		       | rgb[3 * i + 2];
	}
}

void DGC8_Reset(dgc8_t *c)
{
	c->sent_valid = 0;
}

void DGC8_Plan(dgc8_t *c, const uint32_t *pal, int presentation,
               dgc8_commit_t *out)
{
	out->send = !c->sent_valid || memcmp(c->sent, pal, sizeof(c->sent)) != 0;
	out->damage_all = out->send && presentation != DGC8_PRES_EXCLUSIVE;

	if (out->send)
	{
		memcpy(c->sent, pal, sizeof(c->sent));
		c->sent_valid = 1;
	}
}
