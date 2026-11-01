#ifndef DOOM_GENERIC
#define DOOM_GENERIC

#include <stdlib.h>
#include <stdint.h>

#ifndef DOOMGENERIC_RESX
#define DOOMGENERIC_RESX 640
#endif  // DOOMGENERIC_RESX

#ifndef DOOMGENERIC_RESY
#define DOOMGENERIC_RESY 400
#endif  // DOOMGENERIC_RESY


#ifdef CMAP256

typedef uint8_t pixel_t;

#else  // CMAP256

typedef uint32_t pixel_t;

#endif  // CMAP256


extern pixel_t* DG_ScreenBuffer;

// Non-zero when the back end reads I_VideoBuffer itself: I_FinishUpdate then
// leaves DG_ScreenBuffer alone and only calls DG_DrawFrame.  Set it in DG_Init.
extern int DG_DirectVideoBuffer;

// The wayland_c8 configuration value, "auto" or "off": whether the Wayland
// back end may draw paletted (C8) buffers.  Other back ends ignore it.
extern char *DG_C8Mode;

#ifdef __cplusplus
extern "C" {
#endif

void doomgeneric_Create(int argc, char **argv);
void doomgeneric_Tick();


//Implement below functions for your platform
void DG_Init();
void DG_DrawFrame();
void DG_SleepMs(uint32_t ms);
uint32_t DG_GetTicksMs();
int DG_GetKey(int* pressed, unsigned char* key);
void DG_SetWindowTitle(const char * title);

#ifdef __cplusplus
}
#endif

#endif //DOOM_GENERIC
