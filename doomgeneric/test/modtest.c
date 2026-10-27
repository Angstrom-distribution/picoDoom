// Host test of the music module (i_alsamusic.c) with the engine and the
// sound module stubbed: index lookup, file load, register, play, render,
// pause, stop, unregister, and the silent paths.
// Usage: modtest <dir with index.txt and the flac> <lump file> <reference.raw>
// The directory is passed through DOOM_MUSIC_DIR, or -musicdir with "arg".

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../i_alsamusic.h"
#include "../i_sound.h"
#include "../m_argv.h"

#include "../i_alsamusic.c"
#include "../i_musindex.c"
#include "../i_musdec_flac.c"
#include "../i_musplay.c"

char **myargv;
int myargc;
static int rate = 22050;

int I_ALSA_SoundRate(void) { return rate; }
void I_ALSA_Service(int deep) { }

int M_CheckParmWithArgs(char *check, int num_args)
{
    int i;

    for (i = 1; i + num_args < myargc; ++i)
    {
        if (!strcmp(check, myargv[i]))
        {
            return i;
        }
    }
    return 0;
}

static int nfail;

static void report(const char *n, int ok)
{
    printf("%-52s %s\n", n, ok ? "PASS" : "FAIL");
    nfail += !ok;
}

int main(int argc, char **argv)
{
    FILE *f;
    byte lump[200000];
    size_t lumplen;
    int16_t ref[100001], out[512];
    void *h, *h2;
    int i, gain, ok, frames = 0;
    char *args[3] = { "x", "-musicdir", NULL };
    music_module_t *m = &DG_music_module;

    f = fopen(argv[2], "rb");
    lumplen = fread(lump, 1, sizeof(lump), f);
    fclose(f);
    f = fopen(argv[3], "rb");
    ok = fread(ref, 2, 100001, f) == 100001;
    fclose(f);

    // 1. no index: init succeeds, everything is silent
    setenv("DOOM_MUSIC_DIR", "/nonexistent-music-dir", 1);
    report("Init with no index returns true", m->Init());
    report("RegisterSong with no index is NULL", m->RegisterSong(lump, lumplen) == NULL);
    m->PlaySong(NULL, true);
    report("PlaySong(NULL) does nothing", !m->MusicIsPlaying());
    m->Shutdown();

    // 2. env directory
    setenv("DOOM_MUSIC_DIR", argv[1], 1);
    report("Init with DOOM_MUSIC_DIR", m->Init());
    printf("  music_dir = %s\n", music_dir);
    report("directory comes from the environment", !strcmp(music_dir, argv[1]));
    h = m->RegisterSong(lump, lumplen);
    report("RegisterSong finds the file by hash", h != NULL);
    lump[0] ^= 1;
    report("a lump with another hash is silent (NULL)", m->RegisterSong(lump, lumplen) == NULL);
    lump[0] ^= 1;

    m->SetMusicVolume(127);
    m->PlaySong(h, false);
    report("MusicIsPlaying after PlaySong", m->MusicIsPlaying());
    ok = 1;
    for (i = 0; i < 20; ++i)
    {
        gain = I_ALSA_MusicRender(out, 512);
        ok &= gain == 127 && !memcmp(out, ref + frames, 512 * 2);
        frames += 512;
    }
    report("20 periods equal the reference, gain 127", ok);
    m->PauseMusic();
    gain = I_ALSA_MusicRender(out, 512);
    report("paused: gain 0 and silence", gain == 0 && out[0] == 0 && out[511] == 0);
    m->ResumeMusic();
    gain = I_ALSA_MusicRender(out, 512);
    report("resumed at the same position", gain == 127 && !memcmp(out, ref + frames, 512 * 2));
    m->SetMusicVolume(300);
    gain = I_ALSA_MusicRender(out, 512);
    report("volume is clamped to 127", gain == 127);
    m->SetMusicVolume(-5);
    gain = I_ALSA_MusicRender(out, 512);
    report("volume -5 gives gain 0", gain == 0 && music_gain == 0);
    m->StopSong();
    report("StopSong: not playing, no music", !m->MusicIsPlaying() && I_ALSA_MusicRender(out, 512) == 0);

    // 3. register a second time and unregister the playing song
    h2 = m->RegisterSong(lump, lumplen);
    m->PlaySong(h2, true);
    I_ALSA_MusicRender(out, 512);
    m->UnRegisterSong(h2);
    report("UnRegisterSong of the playing song stops it", !m->MusicIsPlaying());
    m->UnRegisterSong(h);
    m->Shutdown();

    // 4. -musicdir beats the environment
    setenv("DOOM_MUSIC_DIR", "/nonexistent-music-dir", 1);
    args[2] = argv[1];
    myargv = args;
    myargc = 3;
    m->Init();
    report("-musicdir overrides the environment", !strcmp(music_dir, argv[1]) && music_on);
    m->Shutdown();

    // 5. default directory, and a PCM at another rate
    myargc = 1;
    unsetenv("DOOM_MUSIC_DIR");
    m->Init();
    report("default directory is /home/root/doom/music", !strcmp(music_dir, "/home/root/doom/music") && !music_on);
    m->Shutdown();
    rate = 44100;
    m->Init();
    report("PCM at 44100 Hz: music off", !music_on);
    m->Shutdown();
    rate = 0;
    m->Init();
    report("no audio thread: music off", !music_on);
    m->Shutdown();

    printf("%s\n", nfail ? "SOME TESTS FAILED" : "ALL PASSED");
    return nfail != 0;
}
