// Runs the presentation checks on a frame pack, without the engine, and
// compares dg_present's old converter with the converter of the Wayland back
// end as it was before dg_present existed (a1ceef6), when that is linked in.
//
//   present_pak_test frames.pak
//
// The frames of a pack are 35 game frames apart, so the damage checks see
// large changes here; present_hl checks every frame.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dg_present.h"
#include "pak_read.h"
#include "present_check.h"

#ifdef WITH_REF
void ref_setup(int w, int h, int px, int py, int pw, int ph, int bpp);
void ref_convert(void *data, const uint8_t *src, const uint16_t *p565,
                 const uint32_t *p8888);
#endif

int main(int argc, char **argv)
{
	pak_rec_t *recs;
	long n, i;
	int fails;

	if (argc < 2 || (n = pak_load(argv[1], &recs)) <= 0)
	{
		fprintf(stderr, "usage: %s frames.pak\n", argv[0]);
		return 2;
	}
	printf("frame pack: %ld records\n", n);

	PC_Init(0);
	for (i = 0; i < n; i++)
	{
		PC_Frame(recs[i].px, recs[i].rgb, (int)recs[i].frame);
	}
	fails = PC_Report();

#ifdef WITH_REF
	{
		// T4: DGP_ConvertOld against the a1ceef6 converter, three
		// geometries, both formats, every record.
		static const int geo[4][6] =
		{
			{ 240, 150, 20, 0, 200, 150 },
			{ 240, 302, 0, 61, 240, 180 },
			{ 320, 240, 0, 0, 320, 240 },
			{ 201, 151, 0, 0, 201, 151 }
		};
		static uint8_t a[400000], b[400000];
		long bad = 0, cmp = 0;
		int g, bpp;

		for (g = 0; g < 4; g++)
		{
			for (bpp = 2; bpp <= 4; bpp += 2)
			{
				dgp_geom_t geom = { 0 };
				size_t size = (size_t)geo[g][0] * geo[g][1] * bpp;

				DGP_GeomSet(&geom, geo[g][0], geo[g][1], geo[g][2],
				            geo[g][3], geo[g][4], geo[g][5], bpp);
				ref_setup(geo[g][0], geo[g][1], geo[g][2], geo[g][3],
				          geo[g][4], geo[g][5], bpp);
				for (i = 0; i < n; i++)
				{
					uint16_t p565[256];
					uint32_t p8888[256];
					int k;

					for (k = 0; k < 256; k++)
					{
						unsigned r = recs[i].rgb[k * 3];
						unsigned gg = recs[i].rgb[k * 3 + 1];
						unsigned bb = recs[i].rgb[k * 3 + 2];

						p565[k] = ((r >> 3) << 11) | ((gg >> 2) << 5) | (bb >> 3);
						p8888[k] = (r << 16) | (gg << 8) | bb;
					}
					memset(a, 0x5A, size);
					memset(b, 0x5A, size);
					DGP_ConvertOld(a, &geom, recs[i].px, p565, p8888, NULL);
					ref_convert(b, recs[i].px, p565, p8888);
					cmp++;
					if (memcmp(a, b, size) != 0)
					{
						bad++;
					}
				}
				DGP_GeomFree(&geom);
			}
		}
		printf("T4: DGP_ConvertOld against the a1ceef6 converter: %ld comparisons, %ld mismatches\n",
		       cmp, bad);
		fails += (int)bad;
	}
#endif

	printf("PRESENT_PAK_TEST: %s\n", fails == 0 ? "PASS" : "FAIL");
	free(recs);

	return fails != 0;
}
