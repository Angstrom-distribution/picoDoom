// The RGB565 converter, included twice by dg_present.c: once with a
// halfword palette and once with a word palette, which saves the shift that
// ARMv5 needs to index halfwords. Not a header.
//
// The including file defines DGP_PALT (the palette element type), DGP_SUFFIX
// (to tell the static kernels apart) and DGP_CONVERT (the function name).

// Width 200: outputs at source columns 0 2 4 5 7, then 8 more per period.
static void DGP_CAT(kernel200_, DGP_SUFFIX)(dgp_u32 *d, const uint8_t *s,
                                            const DGP_PALT *pal, int groups)
{
	while (groups-- > 0)
	{
		unsigned s0 = s[0], s2 = s[2], s4 = s[4], s5 = s[5], s7 = s[7];
		unsigned s8 = s[8], s10 = s[10], s12 = s[12], s13 = s[13];
		unsigned s15 = s[15];

		d[0] = PAIR(s0, s2);
		d[1] = PAIR(s4, s5);
		d[2] = PAIR(s7, s8);
		d[3] = PAIR(s10, s12);
		d[4] = PAIR(s13, s15);
		d += 5;
		s += 16;
	}
}

// Width 240: outputs at source columns 0 2 3, then 4 more per period.
static void DGP_CAT(kernel240_, DGP_SUFFIX)(dgp_u32 *d, const uint8_t *s,
                                            const DGP_PALT *pal, int groups)
{
	while (groups-- > 0)
	{
		unsigned s0 = s[0], s2 = s[2], s3 = s[3], s4 = s[4], s6 = s[6];
		unsigned s7 = s[7];

		d[0] = PAIR(s0, s2);
		d[1] = PAIR(s3, s4);
		d[2] = PAIR(s6, s7);
		d += 3;
		s += 8;
	}
}

// Width 320: one to one.
static void DGP_CAT(kernel320_, DGP_SUFFIX)(dgp_u32 *d, const uint8_t *s,
                                            const DGP_PALT *pal, int groups)
{
	while (groups-- > 0)
	{
		unsigned s0 = s[0], s1 = s[1], s2 = s[2], s3 = s[3];
		unsigned s4 = s[4], s5 = s[5], s6 = s[6], s7 = s[7];

		d[0] = PAIR(s0, s1);
		d[1] = PAIR(s2, s3);
		d[2] = PAIR(s4, s5);
		d[3] = PAIR(s6, s7);
		d += 4;
		s += 8;
	}
}

void DGP_CONVERT(uint16_t *pic, int stride_px, const uint8_t *src,
                 const DGP_PALT *pal, const dgp_geom_t *g,
                 const uint8_t *convert_rows)
{
	const uint16_t *xoff = g->xoff;
	int w = g->picw, h = g->pich;
	int next = 0;
	int y;

	for (y = 0; y < h; y++)
	{
		uint16_t *row = pic + (size_t)y * stride_px;
		const uint8_t *s;
		int kernel = g->kernel;
		int i = 0;

		if (convert_rows && !convert_rows[y])
		{
			continue;
		}

		if (y > 0 && g->yrow[y] == g->yrow[y - 1])
		{
			memcpy(row, row - stride_px, w * 2);
			continue;
		}

		s = src + g->ymap[y];

		// The next row that is converted from a different source row. Its
		// ten cache lines are fetched while this one is converted.
		if (next <= y)
		{
			next = y + 1;
		}
		while (next < h && (g->yrow[next] == g->yrow[y]
		    || (convert_rows && !convert_rows[next])))
		{
			next++;
		}
		if (next < h)
		{
			const uint8_t *ns = src + g->ymap[next];
			int k;

			for (k = 0; k < DGP_SRC_W; k += 32)
			{
				__builtin_prefetch(ns + k);
			}
		}

		// A row that does not start on a word boundary: one halfword, then
		// the pairs line up. The unrolled kernels need the pattern phase.
		if ((uintptr_t)row & 2)
		{
			row[0] = pal[s[xoff[0]]];
			i = 1;
			kernel = DGP_K_GENERIC;
		}

		if (kernel == DGP_K_200)
		{
			i = w / 10;
			DGP_CAT(kernel200_, DGP_SUFFIX)((dgp_u32 *)row, s, pal, i);
			i *= 10;
		}
		else if (kernel == DGP_K_240)
		{
			i = w / 6;
			DGP_CAT(kernel240_, DGP_SUFFIX)((dgp_u32 *)row, s, pal, i);
			i *= 6;
		}
		else if (kernel == DGP_K_320)
		{
			i = w / 8;
			DGP_CAT(kernel320_, DGP_SUFFIX)((dgp_u32 *)row, s, pal, i);
			i *= 8;
		}

		for (; i + 8 <= w; i += 8)
		{
			unsigned a0 = s[xoff[i]], a1 = s[xoff[i + 1]];
			unsigned a2 = s[xoff[i + 2]], a3 = s[xoff[i + 3]];
			unsigned a4 = s[xoff[i + 4]], a5 = s[xoff[i + 5]];
			unsigned a6 = s[xoff[i + 6]], a7 = s[xoff[i + 7]];
			dgp_u32 *d = (dgp_u32 *)(row + i);

			d[0] = PAIR(a0, a1);
			d[1] = PAIR(a2, a3);
			d[2] = PAIR(a4, a5);
			d[3] = PAIR(a6, a7);
		}
		for (; i + 2 <= w; i += 2)
		{
			unsigned a0 = s[xoff[i]], a1 = s[xoff[i + 1]];

			*(dgp_u32 *)(row + i) = PAIR(a0, a1);
		}
		if (i < w)
		{
			row[i] = pal[s[xoff[i]]];
		}
	}
}
