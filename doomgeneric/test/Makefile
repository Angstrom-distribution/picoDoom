# Host test of the sound effects mixer. The test includes ../i_sndmix.c.
#
#   make              native build and run
#   make armel        static armel build, run under qemu-arm-static
#
# Usage: ./sndmix_test [render.wav [freedoom1.wad]]
# WAD is the Freedoom IWAD the sfx cases read; render.wav is written.
WAD ?= freedoom1.wad

CFLAGS ?= -O2 -Wall
INC = -I.. -I../..

all: sndmix_test
	./sndmix_test render.wav $(WAD)

sndmix_test: sndmix_test.c ../i_sndmix.c ../i_sndmix.h
	$(CC) $(CFLAGS) $(INC) -o $@ sndmix_test.c -lm

armel: sndmix_test_armel
	qemu-arm-static ./sndmix_test_armel render-armel.wav $(WAD)

sndmix_test_armel: sndmix_test.c ../i_sndmix.c ../i_sndmix.h
	arm-linux-gnueabi-gcc $(CFLAGS) -fsigned-char -march=armv5te -mfloat-abi=soft -static $(INC) -o $@ sndmix_test.c -lm

clean:
	rm -f sndmix_test sndmix_test_armel render.wav render-armel.wav

.PHONY: all armel clean
