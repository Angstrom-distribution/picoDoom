// SPDX-License-Identifier: GPL-2.0-or-later
//
// Host test of the WAD inspection (d_wadinfo.c) on the synthetic files that
// gen_wads.py writes: what is accepted as the main WAD and as an add-on, the
// message for what is not, and the game named by an id24 GAMECONF lump.
//
//   wadinfo_test DIR

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../d_wadinfo.c"

void I_Error(char *error, ...)
{
	va_list ap;

	va_start(ap, error);
	vfprintf(stderr, error, ap);
	va_end(ap);
	exit(1);
}

static const char *dir;
static int nfail;

static void report(const char *what, int ok)
{
	printf("%-58s %s\n", what, ok ? "PASS" : "FAIL");
	nfail += !ok;
}

static int scan(const char *file, wadscan_t *s, char *err)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/%s", dir, file);
	return D_ScanWAD(path, s, err, 256);
}

// as_iwad: expected result of the main-WAD check; as_pwad: of the -file check.
// A non-NULL want_i or want_p is a text that must be in the refusal message of
// that check.
static void expect(const char *file, int as_iwad, int as_pwad,
                   const char *want_i, const char *want_p)
{
	wadscan_t s;
	char err[256] = "", erri[256] = "", errp[256] = "", msg[300];
	int r = scan(file, &s, err);
	int ri = r, rp = r, ok;

	if (r == 0)
	{
		ri = D_CheckIWADScan(&s, "x.wad", erri, sizeof(erri));
		rp = D_CheckPWADScan(&s, "x.wad", errp, sizeof(errp));
	}
	else
	{
		strcpy(erri, err);
		strcpy(errp, err);
	}

	ok = (ri == 0) == as_iwad && (rp == 0) == as_pwad &&
	     (want_i == NULL || strstr(erri, want_i) != NULL) &&
	     (want_p == NULL || strstr(errp, want_p) != NULL);
	snprintf(msg, sizeof(msg), "%s: main WAD %s, -file %s", file,
	         ri == 0 ? "ok" : "refused", rp == 0 ? "ok" : "refused");
	report(msg, ok);
	if (ri != 0)
		printf("    main WAD: %s\n", erri);
	if (rp != 0)
		printf("    -file:    %s\n", errp);
}

static void mission(const char *file, GameMission_t want)
{
	wadscan_t s;
	char err[256], msg[300];

	snprintf(msg, sizeof(msg), "%s: GAMECONF names mission %d", file, want);
	report(msg, scan(file, &s, err) == 0 && s.gameconf_mission == want);
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage: wadinfo_test DIR\n");
		return 2;
	}
	dir = argv[1];

	expect("iwad-doom2.wad", 1, 1, NULL, NULL);
	expect("iwad-doom1.wad", 1, 1, NULL, NULL);
	expect("iwad-rerelease-doom2.wad", 1, 1, NULL, NULL);
	expect("pwad-maps.wad", 0, 1, "load it with -file", NULL);
	expect("pwad-addon-episode.wad", 0, 1, "no level data", NULL);
	expect("pwad-resources.wad", 0, 1, "no level data", NULL);
	expect("iwad-no-levels.wad", 0, 1, "no level data", NULL);
	expect("iwad-no-playpal.wad", 0, 1, "no PLAYPAL", NULL);
	expect("pwad-deh21.wad", 0, 1, NULL, NULL);
	expect("pwad-id24-2024.wad", 0, 0, "PLAYPAL", "id24 DEHACKED");
	expect("pwad-id24.wad", 0, 0, "PLAYPAL", "id24 DEHACKED");
	expect("empty.wad", 0, 1, "no level data", NULL);
	expect("not-a-wad.wad", 0, 0, "not a WAD file", "not a WAD file");
	expect("short.wad", 0, 0, "not a WAD file", "not a WAD file");
	expect("bad-directory.wad", 0, 0, "damaged", "damaged");
	expect("missing.wad", 0, 0, "cannot open", "cannot open");

	mission("iwad-rerelease-plut.wad", pack_plut);
	mission("iwad-rerelease-tnt.wad", pack_tnt);
	mission("iwad-rerelease-doom2.wad", doom2);
	mission("iwad-rerelease-doom.wad", doom);
	mission("iwad-doom2.wad", none);

	{
		wadscan_t s;
		char err[256];

		scan("iwad-rerelease-plut.wad", &s, err);
		report("re-release IWAD: GAMECONF and DMENUPIC seen",
		       s.has_gameconf && s.has_dmenupic && s.is_iwad);
		scan("pwad-id24.wad", &s, err);
		report("id24 PWAD: DEHACKED version 2021 seen",
		       s.id24_deh && !s.is_iwad && s.numlumps == 4);
		scan("pwad-deh21.wad", &s, err);
		report("classic PWAD: DEHACKED version 21 is not id24", !s.id24_deh);
	}

	report("GAMECONF without a title", D_MissionFromGameconf("{}", 2) == none);
	report("GAMECONF with a cut-off title",
	       D_MissionFromGameconf("\"title\": \"Doo", 14) == none);
	report("GAMECONF title of another game",
	       D_MissionFromGameconf("\"title\": \"Chex\"", 15) == none);

	printf("%s\n", nfail ? "FAILED" : "all passed");
	return nfail != 0;
}
