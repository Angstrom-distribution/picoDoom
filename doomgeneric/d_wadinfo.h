// SPDX-License-Identifier: GPL-2.0-or-later
//
// Inspect a WAD file by its header and directory, before it is loaded.
//
// Used to refuse files the engine cannot run with a message that says why,
// and to tell the 2019 and later Doom releases (which carry id24 lumps)
// from the classic IWADs. Only the header, the directory and a few small
// lumps are read.

#ifndef __D_WADINFO__
#define __D_WADINFO__

#include <stddef.h>

#include "d_mode.h"

typedef struct
{
	int is_iwad;        // header id is IWAD, otherwise PWAD
	int numlumps;
	int has_playpal;
	int has_colormap;
	int has_texture1;
	int has_e1m1;
	int has_map01;
	int has_gameconf;   // id24 GAMECONF lump (2019 and later releases)
	int has_dmenupic;
	int id24_deh;       // DEHACKED lump of version 2021 (MBF21) or 2024 (id24)
	GameMission_t gameconf_mission;  // from the GAMECONF title, or none
} wadscan_t;

// Reads the header and directory of path. Returns 0 on success, -1 with a
// message in err when the file is unreadable or is not a WAD.
int D_ScanWAD(const char *path, wadscan_t *scan, char *err, size_t errlen);

// Checks a scanned file for use as the main IWAD. Returns 0 when it can be
// used, -1 with a message in err otherwise.
int D_CheckIWADScan(const wadscan_t *scan, const char *path,
                    char *err, size_t errlen);

// Checks a scanned file for use with -file. Returns 0 or -1 as above.
int D_CheckPWADScan(const wadscan_t *scan, const char *path,
                    char *err, size_t errlen);

// Maps the "title" of an id24 GAMECONF lump to a game. none if unknown.
GameMission_t D_MissionFromGameconf(const char *json, size_t len);

// Scan the file and call I_Error with the message when it is refused.
void D_RequireUsableIWAD(const char *path);
void D_RequireUsablePWAD(const char *path);

#endif
