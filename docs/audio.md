# Sound and music

## Sound effects

`i_sndmix.c` is the mixer. It has no ALSA, zone or globals, and the per-sample code is integer and multiplies only, with no divide and no float helper. Sixteen voices read the unsigned 8-bit lumps at their own rate with a 16.16 step, scaled by a left and a right gain from the volume and separation that `s_sound.c` computes, and the sum is clamped to 16 bits. The accumulator is zeroed by the caller once and cleared by `SNDMIX_Output` after it reads each entry, so `SNDMIX_Mix` is leaf code.

`i_alsapcm.c` opens `/dev/snd/pcmC<card>D<device>p` and negotiates the parameters with six ioctls, none of them with a timespec, so the program links statically on ARMv5 without libasound. `i_alsasound.c` is the `sound_module_t` of the dispatcher in `i_sound.c`. In thread mode one audio thread mixes a period and writes it with a blocking write; one mutex guards the voice table and the game thread holds it only for a few assignments. In tick mode the game thread fills the queue itself.

On the h2200 the codec is a UDA1380 on `hw:0,0` that accepts S16 stereo from 8 to 48 kHz. The default output is 22050 Hz, 512-frame periods, four periods.

## Music

`i_alsamusic.c` and `i_musdec_flac.c` play pre-rendered tracks. `RegisterSong` hashes the music lump with FNV-1a, looks the hash up in `index.txt` in the music directory, reads the whole FLAC file into memory and decodes it into a ring buffer. The audio thread, or the tick fill, decodes more when the ring runs low and mixes the music as one mono source with a gain from `SetMusicVolume`. Looping happens inside one fill, so the loop seam is sample-exact. A song with no entry, an unreadable file or a decode error plays as silence and the game keeps running.

The decoder is dr_flac, vendored in `dr_flac.h` under its public-domain or MIT-0 terms. The decoder interface is pluggable, so a format that costs less to decode can replace it.

### Pack format

A music directory holds `index.txt`, one line per song with the hexadecimal FNV-1a hash of the lump and the file name, and the files themselves: FLAC, mono, 22050 Hz, 16 bit. A looping song is rendered as one exact loop. The pack of a WAD is made by rendering each music lump, here with libADLMIDI and the WAD's own GENMIDI bank; a pack rendered from a WAD is derived from that WAD and has its licence, so a pack made from a commercial WAD is not published or committed. The tool that renders a pack is not in this repository yet.
