# Host tests of the mixer and the music module's engine-free code.
#
#   make              native build and run, both tests
#   make armel        static armel build, run under qemu-arm-static
#
# sndmix_test [render.wav [freedoom1.wad]]
#   WAD is the Freedoom IWAD the sfx cases read; render.wav is written.
# musplay_test <file.flac> <reference.raw> [freedoom1.wad]
#   The inputs are made in $(WORK) from a synthetic signal by gen_music_input.py,
#   flac and `flac -d`. They are test inputs, never committed (.gitignore).

WAD ?= freedoom1.wad
WORK ?= work
FLAC ?= flac

CFLAGS ?= -O2 -Wall
INC = -I.. -I../..
ARMFLAGS = -fsigned-char -march=armv5te -mfloat-abi=soft -static

MUSSRC = musplay_test.c ../i_musindex.c ../i_musdec_flac.c ../i_musplay.c \
         ../i_sndmix.c ../i_musindex.h ../i_musdec.h ../i_musplay.h \
         ../i_sndmix.h ../dr_flac.h

all: sndmix_test musplay_test $(WORK)/music.flac $(WORK)/music.raw
	./sndmix_test render.wav $(WAD)
	./musplay_test $(WORK)/music.flac $(WORK)/music.raw $(WAD)

$(WORK)/music.flac: gen_music_input.py
	mkdir -p $(WORK)
	python3 -I gen_music_input.py $(WORK)/music.wav
	$(FLAC) -s -f -5 -b 4096 --no-padding --no-seektable -o $@ $(WORK)/music.wav

$(WORK)/music.raw: $(WORK)/music.flac
	$(FLAC) -s -f -d --force-raw-format --endian=little --sign=signed -o $@ $<

sndmix_test: sndmix_test.c ../i_sndmix.c ../i_sndmix.h
	$(CC) $(CFLAGS) $(INC) -o $@ sndmix_test.c -lm

musplay_test: $(MUSSRC)
	$(CC) $(CFLAGS) $(INC) -o $@ musplay_test.c

armel: sndmix_test_armel musplay_test_armel
	qemu-arm-static ./sndmix_test_armel render-armel.wav $(WAD)
	qemu-arm-static ./musplay_test_armel $(WORK)/music.flac $(WORK)/music.raw $(WAD)

sndmix_test_armel: sndmix_test.c ../i_sndmix.c ../i_sndmix.h
	arm-linux-gnueabi-gcc $(CFLAGS) $(ARMFLAGS) $(INC) -o $@ sndmix_test.c -lm

musplay_test_armel: $(MUSSRC)
	arm-linux-gnueabi-gcc $(CFLAGS) $(ARMFLAGS) $(INC) -o $@ musplay_test.c

clean:
	rm -rf sndmix_test sndmix_test_armel musplay_test musplay_test_armel \
	       render.wav render-armel.wav $(WORK)

.PHONY: all armel clean
