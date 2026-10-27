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
//	Music through the ALSA audio thread: pre-rendered songs, looked up by
//	the hash of the MUS or MIDI lump, whole file in RAM, decoded by the
//	audio thread into a ring and added to the sfx mix. A song without a
//	file is silent.
//

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "i_alsamusic.h"
#include "i_musdec.h"
#include "i_musindex.h"
#include "i_musplay.h"
#include "i_sound.h"
#include "m_argv.h"

#include "doomtype.h"

#define DEFAULT_MUSIC_DIR "/home/root/doom/music"
#define MUSIC_RATE        22050
#define MAX_INDEX_BYTES   (1 << 20)
#define MAX_SONG_BYTES    (64 << 20)

typedef struct
{
    byte *file;                 // whole file, owned
    size_t len;
    uint32_t hash;
    const musdecoder_t *dec;
    void *state;
} song_t;

// music_lock guards mp, current and music_gain. It is never held together
// with the sfx voice lock.

static pthread_mutex_t music_lock = PTHREAD_MUTEX_INITIALIZER;
static musplay_t mp;
static song_t *current;
static int music_gain = 64;
static boolean music_on;
static const char *music_dir;
static musindex_t index_table;

// Called only by the audio thread.

int I_ALSA_MusicRender(int16_t *out, int frames)
{
    int gain = 0;

    pthread_mutex_lock(&music_lock);

    if (MUSPLAY_Render(&mp, out, frames))
    {
        gain = music_gain;
    }

    pthread_mutex_unlock(&music_lock);

    return gain;
}

// Read a whole file into malloc'd memory. NULL on any error.

static byte *ReadFile(const char *path, size_t max, size_t *len)
{
    struct stat st;
    byte *buf;
    size_t got = 0;
    int fd;

    fd = open(path, O_RDONLY);

    if (fd < 0)
    {
        return NULL;
    }

    if (fstat(fd, &st) < 0 || st.st_size <= 0 || (size_t) st.st_size > max)
    {
        close(fd);
        return NULL;
    }

    buf = malloc(st.st_size);

    while (buf != NULL && got < (size_t) st.st_size)
    {
        ssize_t n = read(fd, buf + got, st.st_size - got);

        if (n < 0 && errno == EINTR)
        {
            continue;
        }
        if (n <= 0)
        {
            free(buf);
            buf = NULL;
            break;
        }
        got += n;
    }

    close(fd);
    *len = got;

    return buf;
}

static boolean I_ALSA_InitMusic(void)
{
    char path[512];
    byte *text;
    size_t len;
    int i;
    int rate = I_ALSA_SoundRate();

    if (rate == 0)
    {
        printf("I_ALSA_InitMusic: no audio thread, music off\n");
        return true;
    }

    if (rate != MUSIC_RATE)
    {
        printf("I_ALSA_InitMusic: audio runs at %d Hz, music files are %d Hz, music off\n",
               rate, MUSIC_RATE);
        return true;
    }

    //!
    // @arg <dir>
    //
    // Directory with the pre-rendered music files and their index.txt.
    // The default is the DOOM_MUSIC_DIR environment variable, then
    // /home/root/doom/music.
    //

    i = M_CheckParmWithArgs("-musicdir", 1);

    if (i > 0)
    {
        music_dir = myargv[i + 1];
    }
    else
    {
        music_dir = getenv("DOOM_MUSIC_DIR");

        if (music_dir == NULL || music_dir[0] == '\0')
        {
            music_dir = DEFAULT_MUSIC_DIR;
        }
    }

    snprintf(path, sizeof(path), "%s/index.txt", music_dir);
    text = ReadFile(path, MAX_INDEX_BYTES, &len);

    if (text == NULL)
    {
        printf("I_ALSA_InitMusic: %s: no index, music is silent\n", path);
        return true;
    }

    if (!MUSIDX_Parse(&index_table, (const char *) text, len))
    {
        free(text);
        printf("I_ALSA_InitMusic: out of memory\n");
        return true;
    }

    free(text);

    printf("I_ALSA_InitMusic: %s: %d songs", music_dir, index_table.count);

    if (index_table.rejected > 0)
    {
        printf(", %d bad index lines ignored", index_table.rejected);
    }

    printf("\n");

    music_on = true;

    return true;
}

static void FreeSong(song_t *song)
{
    song->dec->Close(song->state);
    free(song->file);
    free(song);
}

static void I_ALSA_ShutdownMusic(void)
{
    // The audio thread has been joined by the sound module's Shutdown.

    if (current != NULL)
    {
        MUSPLAY_Stop(&mp);
        FreeSong(current);
        current = NULL;
    }

    if (mp.errors > 0)
    {
        printf("I_ALSA_ShutdownMusic: %d decode errors\n", mp.errors);
    }

    MUSIDX_Free(&index_table);
    music_on = false;
}

static void I_ALSA_SetMusicVolume(int volume)
{
    if (volume < 0)
    {
        volume = 0;
    }
    if (volume > 127)
    {
        volume = 127;
    }

    pthread_mutex_lock(&music_lock);
    music_gain = volume;
    pthread_mutex_unlock(&music_lock);
}

static void I_ALSA_PauseMusic(void)
{
    pthread_mutex_lock(&music_lock);
    mp.paused = true;
    pthread_mutex_unlock(&music_lock);
}

static void I_ALSA_ResumeMusic(void)
{
    pthread_mutex_lock(&music_lock);
    mp.paused = false;
    pthread_mutex_unlock(&music_lock);
}

// Game thread, during level load: the only place that reads a file.

static void *I_ALSA_RegisterSong(void *data, int len)
{
    char path[512];
    const char *name;
    const musdecoder_t *dec = NULL;
    unsigned int rate = 0, channels = 0;
    song_t *song;
    uint32_t hash;
    int i;

    if (!music_on || data == NULL || len <= 0)
    {
        return NULL;
    }

    hash = MUSIDX_Hash(data, len);
    name = MUSIDX_Lookup(&index_table, hash);

    if (name == NULL)
    {
        printf("I_ALSA_RegisterSong: no music for %08x\n", hash);
        return NULL;
    }

    song = calloc(1, sizeof(song_t));

    if (song == NULL)
    {
        return NULL;
    }

    snprintf(path, sizeof(path), "%s/%s", music_dir, name);
    song->file = ReadFile(path, MAX_SONG_BYTES, &song->len);
    song->hash = hash;

    if (song->file == NULL)
    {
        printf("I_ALSA_RegisterSong: %08x: cannot read %s\n", hash, path);
        free(song);
        return NULL;
    }

    for (i = 0; musdecoders[i] != NULL; ++i)
    {
        if (musdecoders[i]->Probe(song->file, song->len))
        {
            dec = musdecoders[i];
            break;
        }
    }

    if (dec != NULL)
    {
        song->state = dec->Open(song->file, song->len, &rate, &channels);
    }

    if (song->state == NULL || rate != (unsigned int) I_ALSA_SoundRate()
     || channels != 1)
    {
        if (song->state != NULL)
        {
            dec->Close(song->state);
            printf("I_ALSA_RegisterSong: %08x: %s is %u Hz, %u channels, need %d Hz mono\n",
                   hash, name, rate, channels, I_ALSA_SoundRate());
        }
        else
        {
            printf("I_ALSA_RegisterSong: %08x: %s is not a supported music file\n",
                   hash, name);
        }
        free(song->file);
        free(song);
        return NULL;
    }

    song->dec = dec;

    printf("I_ALSA_RegisterSong: %08x %s %u bytes + %u decoder bytes\n", hash,
           name, (unsigned int) song->len, (unsigned int) dec->Bytes(song->state));

    return song;
}

static void I_ALSA_UnRegisterSong(void *handle)
{
    song_t *song = handle;

    if (song == NULL)
    {
        return;
    }

    pthread_mutex_lock(&music_lock);

    if (current == song)
    {
        MUSPLAY_Stop(&mp);
        current = NULL;
    }

    pthread_mutex_unlock(&music_lock);

    // The audio thread touches the decoder only under music_lock, so it
    // cannot be inside Read for this song any more.

    FreeSong(song);
}

static void I_ALSA_PlaySong(void *handle, boolean looping)
{
    song_t *song = handle;

    if (song == NULL || !music_on)
    {
        return;
    }

    pthread_mutex_lock(&music_lock);

    if (MUSPLAY_Start(&mp, song->dec, song->state, looping))
    {
        current = song;
    }
    else
    {
        MUSPLAY_Stop(&mp);
        current = NULL;
    }

    pthread_mutex_unlock(&music_lock);
}

static void I_ALSA_StopSong(void)
{
    pthread_mutex_lock(&music_lock);
    MUSPLAY_Stop(&mp);
    current = NULL;
    pthread_mutex_unlock(&music_lock);
}

static boolean I_ALSA_MusicIsPlaying(void)
{
    boolean playing;

    pthread_mutex_lock(&music_lock);
    playing = MUSPLAY_IsPlaying(&mp);
    pthread_mutex_unlock(&music_lock);

    return playing;
}

static snddevice_t music_alsa_devices[] =
{
    SNDDEVICE_SB,
    SNDDEVICE_PAS,
    SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS,
    SNDDEVICE_AWE32,
};

// Every slot except Poll is set.

music_module_t DG_music_module =
{
    music_alsa_devices,
    arrlen(music_alsa_devices),
    I_ALSA_InitMusic,
    I_ALSA_ShutdownMusic,
    I_ALSA_SetMusicVolume,
    I_ALSA_PauseMusic,
    I_ALSA_ResumeMusic,
    I_ALSA_RegisterSong,
    I_ALSA_UnRegisterSong,
    I_ALSA_PlaySong,
    I_ALSA_StopSong,
    I_ALSA_MusicIsPlaying,
    NULL,
};
