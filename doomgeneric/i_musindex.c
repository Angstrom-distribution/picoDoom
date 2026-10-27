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
//	Music song identification. See i_musindex.h.
//

#include <stdlib.h>
#include <string.h>

#include "i_musindex.h"

uint32_t MUSIDX_Hash(const byte *data, size_t len)
{
    uint32_t h = 0x811c9dc5;
    size_t i;

    for (i = 0; i < len; ++i)
    {
        h = (h ^ data[i]) * 0x01000193;
    }

    return h;
}

static int HexDigit(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    return -1;
}

// One line, without its newline. Returns 1 for an entry, 0 for a skipped
// line and -1 for a bad one.

static int ParseLine(const char *p, size_t len, musidx_entry_t *e)
{
    size_t i, n;
    uint32_t hash = 0;

    while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == ' ' || p[len - 1] == '\t'))
    {
        --len;
    }

    if (len == 0 || p[0] == '#')
    {
        return 0;
    }

    if (len < 10 || p[8] != ' ')
    {
        return -1;
    }

    for (i = 0; i < 8; ++i)
    {
        int d = HexDigit(p[i]);

        if (d < 0)
        {
            return -1;
        }
        hash = (hash << 4) | d;
    }

    p += 9;
    n = len - 9;

    if (n < 1 || n > MUSIDX_NAME_MAX || p[0] == '.' || memchr(p, '/', n) != NULL
     || memchr(p, '\0', n) != NULL)
    {
        return -1;
    }

    e->hash = hash;
    memcpy(e->name, p, n);
    e->name[n] = '\0';

    return 1;
}

boolean MUSIDX_Parse(musindex_t *idx, const char *text, size_t len)
{
    size_t pos = 0, cap = 0;
    musidx_entry_t e;
    int r, i;

    idx->entries = NULL;
    idx->count = 0;
    idx->rejected = 0;

    while (pos < len)
    {
        const char *nl = memchr(text + pos, '\n', len - pos);
        size_t linelen = nl != NULL ? (size_t) (nl - (text + pos)) : len - pos;

        r = ParseLine(text + pos, linelen, &e);
        pos += linelen + 1;

        if (r < 0)
        {
            ++idx->rejected;
            continue;
        }
        if (r == 0)
        {
            continue;
        }

        for (i = 0; i < idx->count; ++i)
        {
            if (idx->entries[i].hash == e.hash)
            {
                break;
            }
        }
        if (i < idx->count)
        {
            continue;
        }

        if ((size_t) idx->count == cap)
        {
            musidx_entry_t *grown;

            cap = cap ? 2 * cap : 64;
            grown = realloc(idx->entries, cap * sizeof(musidx_entry_t));

            if (grown == NULL)
            {
                MUSIDX_Free(idx);
                return false;
            }
            idx->entries = grown;
        }

        idx->entries[idx->count++] = e;
    }

    return true;
}

const char *MUSIDX_Lookup(const musindex_t *idx, uint32_t hash)
{
    int i;

    for (i = 0; i < idx->count; ++i)
    {
        if (idx->entries[i].hash == hash)
        {
            return idx->entries[i].name;
        }
    }

    return NULL;
}

void MUSIDX_Free(musindex_t *idx)
{
    free(idx->entries);
    idx->entries = NULL;
    idx->count = 0;
}
