// The checks of the presentation path, fed one frame at a time. The game
// feeds them through present_hl.c, a frame pack through present_pak_test.c.

#ifndef PRESENT_CHECK_H
#define PRESENT_CHECK_H

#include <stdint.h>

#define PC_FRAME_BYTES (320 * 200)

// quick: fewer geometries and simulations, for slow machines.
void PC_Init(int quick);
// src is the 320x200 indexed frame, rgb the 256 palette entries as r, g, b.
void PC_Frame(const uint8_t *src, const uint8_t *rgb, int frameno);
// Prints the results. Returns the number of failures (0 = all passed).
int PC_Report(void);

#endif
