# Sound and music

## Sound effects

`i_sndmix.c` is the mixer. It has no ALSA, zone or globals, and the per-sample code is integer and multiplies only, with no divide and no float helper. Sixteen voices read the unsigned 8-bit lumps at their own rate with a 16.16 step, scaled by a left and a right gain from the volume and separation that `s_sound.c` computes, and the sum is clamped to 16 bits. The accumulator is zeroed by the caller once and cleared by `SNDMIX_Output` after it reads each entry, so `SNDMIX_Mix` is leaf code.

`i_alsapcm.c` opens `/dev/snd/pcmC<card>D<device>p` and sets the parameters with a few ioctls, none of which carries a timespec, so the program links statically on ARMv5 without libasound. `i_alsasound.c` is the `sound_module_t` of the dispatcher in `i_sound.c`. In thread mode one audio thread mixes a period and writes it with a blocking write; one mutex guards the voice table and the game thread holds it only for a few assignments. In tick mode the game thread fills the queue itself.

On the h2200 the codec is a UDA1380 on `hw:0,0` that accepts S16 stereo from 8 to 48 kHz. The default output is 22050 Hz, 512-frame periods, four periods.

## Music

`i_alsamusic.c` and `i_musdec_flac.c` play pre-rendered tracks. `RegisterSong` hashes the music lump with FNV-1a, looks the hash up in `index.txt` in the music directory, reads the whole FLAC file into memory and decodes it into a ring buffer. The audio thread, or the tick fill, decodes more when the ring runs low and mixes the music as one mono source with a gain from `SetMusicVolume`. Looping happens inside one fill, so the loop seam is sample-exact. A song with no entry, an unreadable file or a truncated file plays as silence and the game keeps running. The decoder does not check the FLAC CRC, so a damaged file can play as noise.

The decoder is dr_flac, vendored in `dr_flac.h` under its public-domain or MIT-0 terms. The decoder interface is pluggable, so a format that costs less to decode can replace it.

### Pack format

A music directory holds `index.txt`, one line per song: the FNV-1a hash of the lump as 8 lowercase hexadecimal digits, one space, then the file name (`1a2b3c4d name.flac`), and the files themselves: FLAC, mono, 22050 Hz, 16 bit. Blank lines and lines starting with `#` are skipped, trailing spaces, tabs and a carriage return are ignored, a line in another format is counted as bad and ignored, and the first line wins when two have the same hash; a file name is 1 to 63 bytes, with no `/` and no leading dot. A file that is not mono or not at 22050 Hz is refused with a message and the song is silent. A file is read whole into memory, at most 64 MiB, so keep songs small on a 64 MiB board. A looping song is rendered as one exact loop. The pack of a WAD is made by rendering each music lump, here with libADLMIDI and the WAD's own GENMIDI bank; a pack rendered from a WAD is derived from that WAD and has its licence, so a pack made from a commercial WAD is not published or committed. A tool that renders a pack is a plan; it is not in this repository.
