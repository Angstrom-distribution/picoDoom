// Reads the frame pack that present_hl -dumpframes writes.

#ifndef PAK_READ_H
#define PAK_READ_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
	uint32_t frame, flags;
	uint8_t rgb[768];
	uint8_t px[320 * 200];
} pak_rec_t;

static inline uint32_t pak_get32(const uint8_t *b)
{
	return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24);
}

// Loads the whole pack. Returns the record count, or -1.
static inline long pak_load(const char *path, pak_rec_t **out)
{
	FILE *f = fopen(path, "rb");
	char magic[8];
	long n = 0, cap = 0;
	pak_rec_t *recs = NULL;
	uint8_t h[8];

	if (!f || fread(magic, 1, 8, f) != 8 || memcmp(magic, "DGFPAK1", 8) != 0)
	{
		if (f)
		{
			fclose(f);
		}
		return -1;
	}

	for (;;)
	{
		pak_rec_t *r;

		if (fread(h, 1, 8, f) != 8)
		{
			break;
		}
		if (n == cap)
		{
			cap = cap ? cap * 2 : 256;
			recs = realloc(recs, cap * sizeof(*recs));
			if (!recs)
			{
				fclose(f);
				return -1;
			}
		}
		r = &recs[n];
		r->frame = pak_get32(h);
		r->flags = pak_get32(h + 4);
		if (fread(r->rgb, 1, 768, f) != 768
		 || fread(r->px, 1, 320 * 200, f) != 320 * 200)
		{
			break;
		}
		n++;
	}

	fclose(f);
	*out = recs;

	return n;
}

#endif
