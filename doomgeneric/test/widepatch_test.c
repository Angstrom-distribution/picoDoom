// SPDX-License-Identifier: GPL-2.0-or-later
//
// Host test of V_DrawPatch and V_DrawPatchFlipped (v_video.c) with patches
// wider than the screen, the way the 2019 and later Doom releases ship their
// full-screen graphics: the middle columns are drawn, the outer ones are
// dropped. Patches that fit are drawn as before. The patches are built here,
// with widths chosen for the test.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../v_video.c"

// Stand-ins for the rest of the engine that v_video.c links against.
byte *I_VideoBuffer;
int usemouse, mouse_threshold;
float mouse_acceleration;
void I_Error(char *error, ...) { fprintf(stderr, "I_Error: %s\n", error); exit(1); }
int I_GetPaletteIndex(int r, int g, int b) { return 0; }
void M_AddToBox(fixed_t *box, fixed_t x, fixed_t y) {}
boolean M_FileExists(char *f) { return false; }
boolean M_WriteFile(char *n, void *s, int l) { return false; }
int M_snprintf(char *b, size_t n, const char *f, ...) { return 0; }
void *W_CacheLumpName(char *n, int t) { return NULL; }
void Z_Free(void *p) { free(p); }
void *Z_Malloc(int s, int t, void *u) { return malloc(s); }

static int nfail;

static void report(const char *what, int ok)
{
	printf("%-62s %s\n", what, ok ? "PASS" : "FAIL");
	nfail += !ok;
}

// Column c holds one post of height rows, every pixel c % 250 + 1, so the
// source column of a screen column can be read back from the screen.
static patch_t *make_patch(int width, int height, int left, int top)
{
	size_t colsize = 3 + height + 1 + 1;
	size_t hdr = 8 + 4 * width;
	byte *p = calloc(1, hdr + colsize * width);
	int c;

	*(int16_t *) (p + 0) = width;
	*(int16_t *) (p + 2) = height;
	*(int16_t *) (p + 4) = left;
	*(int16_t *) (p + 6) = top;
	for (c = 0; c < width; c++)
	{
		byte *col = p + hdr + colsize * c;

		*(int32_t *) (p + 8 + 4 * c) = hdr + colsize * c;
		col[0] = 0;
		col[1] = height;
		memset(col + 3, c % 250 + 1, height);
		col[3 + height + 1] = 0xff;
	}
	return (patch_t *) p;
}

static byte screen[SCREENWIDTH * SCREENHEIGHT];

static int src_col(int x) { return screen[x] ? screen[x] - 1 : -1; }

int main(void)
{
	patch_t *p;
	int x, ok;

	V_UseBuffer(screen);

	p = make_patch(400, 200, 0, 0);
	report("V_PatchCenterSkip of a 400 column patch is 40",
	       V_PatchCenterSkip(p) == 40);
	memset(screen, 0, sizeof(screen));
	V_DrawPatch(0, 0, p);
	for (ok = 1, x = 0; x < SCREENWIDTH; x++)
		ok &= src_col(x) == (x + 40) % 250;
	report("400 column patch: columns 40..359 fill the screen", ok);
	report("400 column patch: the last row is drawn too",
	       screen[199 * SCREENWIDTH + 319] == (359 % 250) + 1);
	free(p);

	p = make_patch(449, 32, 0, 0);
	memset(screen, 0, sizeof(screen));
	V_DrawPatch(0, 168, p);
	for (ok = 1, x = 0; x < SCREENWIDTH; x++)
		ok &= screen[168 * SCREENWIDTH + x] == (x + 64) % 250 + 1;
	report("449 column bar at y 168: columns 64..383", ok);
	report("449 column bar: nothing above it",
	       screen[167 * SCREENWIDTH + 5] == 0);
	free(p);

	p = make_patch(361, 200, 0, 0);
	report("V_PatchCenterSkip of a 361 column patch is 20",
	       V_PatchCenterSkip(p) == 20);
	memset(screen, 0, sizeof(screen));
	V_DrawPatchFlipped(0, 0, p);
	for (ok = 1, x = 0; x < SCREENWIDTH; x++)
		ok &= src_col(x) == (361 - 1 - (x + 20)) % 250;
	report("361 column patch, flipped: mirrored middle columns", ok);
	free(p);

	p = make_patch(320, 200, 0, 0);
	report("V_PatchCenterSkip of a 320 column patch is 0",
	       V_PatchCenterSkip(p) == 0);
	memset(screen, 0, sizeof(screen));
	V_DrawPatch(0, 0, p);
	for (ok = 1, x = 0; x < SCREENWIDTH; x++)
		ok &= src_col(x) == x % 250;
	report("320 column patch: drawn unchanged", ok);
	free(p);

	p = make_patch(10, 8, 0, 0);
	memset(screen, 0, sizeof(screen));
	V_DrawPatch(100, 20, p);
	for (ok = 1, x = 0; x < SCREENWIDTH; x++)
		ok &= (screen[20 * SCREENWIDTH + x] != 0) == (x >= 100 && x < 110);
	report("small patch at x 100: drawn where it is put", ok);
	free(p);

	printf("%s\n", nfail ? "FAILED" : "all passed");
	return nfail != 0;
}
