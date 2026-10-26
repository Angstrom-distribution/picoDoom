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
//	Music song identification: FNV-1a 32 of the lump bytes and the parser
//	of index.txt (hash, file name). No file I/O, no engine.
//

#ifndef __I_MUSINDEX__
#define __I_MUSINDEX__

#include "doomtype.h"
#include <stddef.h>
#include <stdint.h>

#define MUSIDX_NAME_MAX 63

typedef struct
{
    uint32_t hash;
    char name[MUSIDX_NAME_MAX + 1];
} musidx_entry_t;

typedef struct
{
    musidx_entry_t *entries;
    int count;
    int rejected;               // lines that broke the rules
} musindex_t;

uint32_t MUSIDX_Hash(const byte *data, size_t len);

// Parses "%08x <file>" lines. Blank lines and lines starting with '#' are
// skipped. A file name is 1..63 bytes, has no '/', and does not start with
// '.'. Bad lines are counted in rejected and ignored. A repeated hash keeps
// its first line. Returns false only if out of memory.

boolean MUSIDX_Parse(musindex_t *idx, const char *text, size_t len);

// The file name for a hash, or NULL.

const char *MUSIDX_Lookup(const musindex_t *idx, uint32_t hash);

void MUSIDX_Free(musindex_t *idx);

#endif
