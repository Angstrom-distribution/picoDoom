// SPDX-License-Identifier: GPL-2.0-or-later
//
// See d_wadinfo.h.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "d_wadinfo.h"

#define MAX_LUMPS 1000000
#define PROBE_LEN 512

static uint32_t rd32(const unsigned char *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24);
}

// Lump names are eight bytes, NUL padded when shorter, and case-insensitive.
static int name_is(const unsigned char *n, const char *s)
{
	int i;

	for (i = 0; i < 8; i++)
	{
		int c = n[i];

		if (c >= 'a' && c <= 'z')
			c -= 'a' - 'A';
		if (c != (unsigned char) s[i])
			return 0;
		if (c == 0)
			return 1;
	}
	return 1;
}

static const char *memfind(const char *h, size_t hl, const char *n)
{
	size_t nl = strlen(n), i;

	for (i = 0; i + nl <= hl; i++)
		if (memcmp(h + i, n, nl) == 0)
			return h + i;
	return NULL;
}

static int contains_nocase(const char *h, const char *n)
{
	size_t nl = strlen(n), i, j;

	for (i = 0; h[i]; i++)
	{
		for (j = 0; j < nl && h[i + j]; j++)
		{
			char a = h[i + j], b = n[j];

			if (a >= 'A' && a <= 'Z')
				a += 'a' - 'A';
			if (a != b)
				break;
		}
		if (j == nl)
			return 1;
	}
	return 0;
}

GameMission_t D_MissionFromGameconf(const char *json, size_t len)
{
	const char *p = memfind(json, len, "\"title\"");
	const char *end = json + len;
	char title[64];
	size_t n = 0;

	if (p == NULL)
		return none;
	p += 7;
	while (p < end && *p != '"')
		p++;
	p++;
	while (p < end && *p != '"' && n < sizeof(title) - 1)
		title[n++] = *p++;
	title[n] = 0;

	if (contains_nocase(title, "plutonia"))
		return pack_plut;
	if (contains_nocase(title, "tnt"))
		return pack_tnt;
	if (contains_nocase(title, "doom ii"))
		return doom2;
	if (contains_nocase(title, "doom"))
		return doom;
	return none;
}

// The lump is read through a small buffer; only a prefix is needed.
static size_t read_prefix(FILE *f, uint32_t pos, uint32_t size, char *buf)
{
	size_t n = size < PROBE_LEN ? size : PROBE_LEN;

	if (fseek(f, pos, SEEK_SET) != 0)
		return 0;
	return fread(buf, 1, n, f);
}

int D_ScanWAD(const char *path, wadscan_t *scan, char *err, size_t errlen)
{
	unsigned char hdr[12], *dir;
	FILE *f;
	long fsize;
	uint32_t n, ofs, i;

	memset(scan, 0, sizeof(*scan));
	scan->gameconf_mission = none;

	f = fopen(path, "rb");
	if (f == NULL)
	{
		snprintf(err, errlen, "%s: cannot open the file", path);
		return -1;
	}
	fseek(f, 0, SEEK_END);
	fsize = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (fread(hdr, 1, 12, f) != 12 ||
	    (memcmp(hdr, "IWAD", 4) != 0 && memcmp(hdr, "PWAD", 4) != 0))
	{
		fclose(f);
		snprintf(err, errlen, "%s is not a WAD file: it has no IWAD or "
		         "PWAD header", path);
		return -1;
	}
	scan->is_iwad = hdr[0] == 'I';
	n = rd32(hdr + 4);
	ofs = rd32(hdr + 8);

	if (n > MAX_LUMPS || (uint64_t) ofs + (uint64_t) n * 16 > (uint64_t) fsize)
	{
		fclose(f);
		snprintf(err, errlen, "%s is damaged: the lump directory (%u "
		         "entries at offset %u) does not fit in the file",
		         path, (unsigned) n, (unsigned) ofs);
		return -1;
	}
	scan->numlumps = n;

	dir = malloc(n ? (size_t) n * 16 : 1);
	if (dir == NULL || fseek(f, ofs, SEEK_SET) != 0 ||
	    fread(dir, 16, n, f) != n)
	{
		free(dir);
		fclose(f);
		snprintf(err, errlen, "%s: cannot read the lump directory", path);
		return -1;
	}

	for (i = 0; i < n; i++)
	{
		const unsigned char *e = dir + i * 16;
		const unsigned char *nm = e + 8;
		uint32_t pos = rd32(e), size = rd32(e + 4);
		char buf[PROBE_LEN + 1];
		size_t got;

		if (name_is(nm, "PLAYPAL"))
			scan->has_playpal = 1;
		else if (name_is(nm, "COLORMAP"))
			scan->has_colormap = 1;
		else if (name_is(nm, "TEXTURE1"))
			scan->has_texture1 = 1;
		else if (name_is(nm, "E1M1"))
			scan->has_e1m1 = 1;
		else if (name_is(nm, "MAP01"))
			scan->has_map01 = 1;
		else if (name_is(nm, "DMENUPIC"))
			scan->has_dmenupic = 1;
		else if (name_is(nm, "GAMECONF"))
		{
			scan->has_gameconf = 1;
			got = read_prefix(f, pos, size, buf);
			scan->gameconf_mission = D_MissionFromGameconf(buf, got);
		}
		else if (name_is(nm, "DEHACKED"))
		{
			got = read_prefix(f, pos, size, buf);
			buf[got] = 0;
			// Patches that use the id24 extensions say 2024 in the
			// header; the Legacy of Rust files say 2021.
			if (memfind(buf, got, "Doom version = 2021") ||
			    memfind(buf, got, "Doom version = 2024"))
				scan->id24_deh = 1;
		}
	}

	free(dir);
	fclose(f);
	return 0;
}

int D_CheckIWADScan(const wadscan_t *s, const char *path,
                    char *err, size_t errlen)
{
	const char *missing = NULL;

	if (!s->has_e1m1 && !s->has_map01)
	{
		snprintf(err, errlen, "%s is not a Doom IWAD: it has no level "
		         "data (E1M1 or MAP01).%s", path, s->is_iwad ? "" :
		         "\nIt is a PWAD: load it with -file, after an IWAD "
		         "given with -iwad.");
		return -1;
	}

	if (!s->has_playpal)
		missing = "PLAYPAL";
	else if (!s->has_colormap)
		missing = "COLORMAP";
	else if (!s->has_texture1)
		missing = "TEXTURE1";

	if (missing != NULL)
	{
		snprintf(err, errlen, "%s cannot be the main WAD: it has levels "
		         "but no %s.%s", path, missing, s->is_iwad ? "" :
		         "\nIt is a PWAD: load it with -file, after an IWAD "
		         "given with -iwad.");
		return -1;
	}

	if (s->id24_deh)
	{
		snprintf(err, errlen, "%s needs id24 DEHACKED extensions, which "
		         "this engine does not implement.", path);
		return -1;
	}

	return 0;
}

int D_CheckPWADScan(const wadscan_t *s, const char *path,
                    char *err, size_t errlen)
{
	if (s->id24_deh)
	{
		snprintf(err, errlen, "%s needs id24 DEHACKED extensions (the "
		         "2019 and later Doom releases' add-ons such as Legacy "
		         "of Rust), which this engine does not implement. "
		         "Expansions and levels for the classic game load "
		         "normally.", path);
		return -1;
	}

	return 0;
}

// I_Error is declared here to keep the checks above free of engine headers.
extern void I_Error(char *error, ...);

void D_RequireUsableIWAD(const char *path)
{
	wadscan_t scan;
	char err[512];

	if (D_ScanWAD(path, &scan, err, sizeof(err)) != 0 ||
	    D_CheckIWADScan(&scan, path, err, sizeof(err)) != 0)
		I_Error("%s", err);
}

void D_RequireUsablePWAD(const char *path)
{
	wadscan_t scan;
	char err[512];

	if (D_ScanWAD(path, &scan, err, sizeof(err)) != 0 ||
	    D_CheckPWADScan(&scan, path, err, sizeof(err)) != 0)
		I_Error("%s", err);
}
