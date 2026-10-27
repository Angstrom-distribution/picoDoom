// Test-only headless back end with scripted keys: types idclev<NN> at a
// given game-start-relative time, quits after RUNSECS seconds. Linked with
// pcmwav.c in place of the video back end and the PCM layer, see
// headless-build.sh. CHEATS="12:20,13:40" types idclev12 at 20 s, idclev13 at 40 s.
// A -timedemo run ends by itself, before RUNSECS if it is short enough.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "doomgeneric.h"
extern void I_Quit(void);
static uint32_t T0; static int started;
void DG_Init(void) {}
void DG_DrawFrame(void) {}
void DG_SleepMs(uint32_t ms) { usleep(ms * 1000); }
uint32_t DG_GetTicksMs(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000 + t.tv_nsec / 1000000; }
void DG_SetWindowTitle(const char *title) {}
// CHEATS="12:20,13:40" = idclev12 at 20 s, idclev13 at 40 s
static char script[64]; static int sp = -1, sdone; static double cheat_t[8]; static char cheat_s[8][8]; static int ncheat, cnext;
int DG_GetKey(int *pressed, unsigned char *key)
{
	static int rel; static unsigned char last;
	double el = (DG_GetTicksMs() - T0) / 1000.0;
	if (!started) return 0;
	if (rel) { rel = 0; *pressed = 0; *key = last; return 1; }
	if (sp < 0 && cnext < ncheat && el >= cheat_t[cnext]) { strcpy(script, "idclev"); strcat(script, cheat_s[cnext]); sp = 0; ++cnext; }
	if (sp >= 0)
	{
		if (!script[sp]) { sp = -1; return 0; }
		*pressed = 1; *key = last = script[sp++]; rel = 1; return 1;
	}
	return 0;
}
int main(int argc, char **argv)
{
	double run = getenv("RUNSECS") ? atof(getenv("RUNSECS")) : 30;
	char *c = getenv("CHEATS") ? strdup(getenv("CHEATS")) : NULL, *tok;
	for (tok = c ? strtok(c, ",") : NULL; tok && ncheat < 8; tok = strtok(NULL, ","))
	{ char *col = strchr(tok, ':'); *col = 0; strcpy(cheat_s[ncheat], tok); cheat_t[ncheat++] = atof(col + 1); }
	doomgeneric_Create(argc, argv);
	T0 = DG_GetTicksMs(); started = 1;
	while ((DG_GetTicksMs() - T0) / 1000.0 < run) doomgeneric_Tick();
	printf("headless: ran %.1f s, quitting\n", run);
	I_Quit();
	return 0;
}
