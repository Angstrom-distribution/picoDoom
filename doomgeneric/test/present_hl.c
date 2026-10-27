// A headless back end for the presentation tests: no display, no input.
//
//   present_hl -iwad freedoom1.wad -timedemo demo1 -nosound [options]
//
// On every DG_DrawFrame it hands the engine's 320x200 indexed frame and the
// palette to the checks in present_check.c, and prints the results when the
// timedemo ends.
//
//   -quick                    a reduced set of simulations, for qemu
//   -dumpframes FILE          write a frame pack: every -dumpstep'th frame
//                             and every frame whose palette changed
//   -dumpstep N               default 35
//
// The frame pack is: "DGFPAK1\0", then records of u32 frame number, u32 flags
// (1 = palette changed, 2 = step), 768 bytes of r, g, b and 64000 indexed
// pixels, all little endian.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "doomgeneric.h"
#include "i_system.h"
#include "i_video.h"
#include "m_argv.h"
#include "present_check.h"

static FILE *s_Dump;
static int s_DumpStep = 35;
static long s_DumpCount;
static uint8_t s_PrevRgb[768];
static int s_PrevValid;
static int s_Frame;
static long s_CopyMismatch;
static int s_Failures;
static int s_Done;

static void put32(FILE *f, uint32_t v)
{
	uint8_t b[4] = { v, v >> 8, v >> 16, v >> 24 };

	fwrite(b, 1, 4, f);
}

static void finish(void)
{
	if (s_Done)
	{
		return;
	}
	s_Done = 1;

	s_Failures = PC_Report();
	printf("DG_ScreenBuffer differs from I_VideoBuffer in %ld frames\n",
	       s_CopyMismatch);
	if (s_Dump)
	{
		fclose(s_Dump);
		printf("frame pack: %ld records\n", s_DumpCount);
	}
	printf("PRESENT_TEST: %s\n", s_Failures == 0 ? "PASS" : "FAIL");
}

void DG_Init(void)
{
	int p;

	PC_Init(M_ParmExists("-quick"));

	p = M_CheckParmWithArgs("-dumpstep", 1);
	if (p)
	{
		s_DumpStep = atoi(myargv[p + 1]);
	}
	p = M_CheckParmWithArgs("-dumpframes", 1);
	if (p)
	{
		s_Dump = fopen(myargv[p + 1], "wb");
		if (!s_Dump)
		{
			perror(myargv[p + 1]);
			exit(2);
		}
		fwrite("DGFPAK1", 1, 8, s_Dump);
	}

	I_AtExit(finish, true);
}

void DG_DrawFrame(void)
{
	uint8_t rgb[768];
	int i, palchanged, step;

	for (i = 0; i < 256; i++)
	{
		rgb[i * 3] = colors[i].r;
		rgb[i * 3 + 1] = colors[i].g;
		rgb[i * 3 + 2] = colors[i].b;
	}

	s_Frame++;

	if (memcmp(DG_ScreenBuffer, I_VideoBuffer, 320 * 200) != 0)
	{
		s_CopyMismatch++;
	}

	palchanged = s_PrevValid && memcmp(rgb, s_PrevRgb, 768) != 0;
	step = s_DumpStep > 0 && s_Frame % s_DumpStep == 0;
	if (s_Dump && (palchanged || step))
	{
		put32(s_Dump, s_Frame);
		put32(s_Dump, (palchanged ? 1 : 0) | (step ? 2 : 0));
		fwrite(rgb, 1, 768, s_Dump);
		fwrite(I_VideoBuffer, 1, 320 * 200, s_Dump);
		s_DumpCount++;
	}
	memcpy(s_PrevRgb, rgb, 768);
	s_PrevValid = 1;

	PC_Frame(I_VideoBuffer, rgb, s_Frame);
}

void DG_SleepMs(uint32_t ms)
{
	usleep(ms * 1000);
}

uint32_t DG_GetTicksMs(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);

	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int DG_GetKey(int *pressed, unsigned char *key)
{
	(void)pressed;
	(void)key;

	return 0;
}

void DG_SetWindowTitle(const char *title)
{
	(void)title;
}

int main(int argc, char **argv)
{
	doomgeneric_Create(argc, argv);

	while (1)
	{
		doomgeneric_Tick();
	}

	return 0;
}
