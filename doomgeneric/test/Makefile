# Host tests of the mixer and the music module's engine-free code.
#
#   make              native build and run, both tests
#   make armel        static armel build, run under qemu-arm-static
#
# sndmix_test [render.wav [freedoom1.wad]]
#   WAD is the Freedoom IWAD the sfx cases read; render.wav is written.
# modtest <dir> <lump> <reference.raw>
#   The music module itself (i_alsamusic.c) with the engine and the sound
#   module stubbed.
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

all: sndmix_test tick_test audiomod_test audiomod_old musplay_test modtest $(WORK)/music.flac $(WORK)/music.raw
	./sndmix_test render.wav $(WAD)
	./tick_test
	./audiomod_test | tee audiomod.out
	./audiomod_old | tee audiomod_old.out
	grep RESULT audiomod.out > audiomod.res; grep RESULT audiomod_old.out | diff - audiomod.res
	./musplay_test $(WORK)/music.flac $(WORK)/music.raw $(WAD)
	./modtest $(WORK)/mod $(WORK)/lump.bin $(WORK)/music.raw

$(WORK)/music.flac: gen_music_input.py
	mkdir -p $(WORK)
	python3 -I gen_music_input.py $(WORK)/music.wav $(WORK)
	$(FLAC) -s -f -5 -b 4096 --no-padding --no-seektable -o $@ $(WORK)/music.wav
	cp $@ $(WORK)/mod/d_e1m1.flac

$(WORK)/music.raw: $(WORK)/music.flac
	$(FLAC) -s -f -d --force-raw-format --endian=little --sign=signed -o $@ $<

sndmix_test: sndmix_test.c ../i_sndmix.c ../i_sndmix.h
	$(CC) $(CFLAGS) $(INC) -o $@ sndmix_test.c -lm

MODSRC = modtest.c ../i_alsamusic.c ../i_alsamusic.h ../i_musindex.c ../i_musdec_flac.c \
         ../i_musplay.c ../i_musdec.h ../i_musindex.h ../i_musplay.h ../dr_flac.h
MODFLAGS = -I.. -I../.. -DFEATURE_SOUND -DNORMALUNIX -DLINUX

modtest: $(MODSRC)
	$(CC) $(CFLAGS) $(MODFLAGS) -o $@ modtest.c

modtest_armel: $(MODSRC)
	arm-linux-gnueabi-gcc $(CFLAGS) $(ARMFLAGS) $(MODFLAGS) -o $@ modtest.c

tick_test: tick_test.c ../i_alsatick.c ../i_alsatick.h
	$(CC) $(CFLAGS) $(INC) -o $@ tick_test.c

tick_test_armel: tick_test.c ../i_alsatick.c ../i_alsatick.h
	arm-linux-gnueabi-gcc $(CFLAGS) $(ARMFLAGS) $(INC) -o $@ tick_test.c

# The sound module on a fake PCM device. audiomod_old is the same test with
# the module as it was before the perf-audio work (git history, $(OLDREV)).

OLDREV ?= a1ceef6
AMODFLAGS = -DFEATURE_SOUND -DNORMALUNIX -DLINUX -D_GNU_SOURCE -pthread
AMODSRC = audiomod_test.c ../i_alsasound.c ../i_alsatick.c ../i_sndmix.c \
          ../i_alsatick.h ../i_alsapcm.h ../i_sndmix.h ../i_alsamusic.h

$(WORK)/i_alsasound_old.c:
	mkdir -p $(WORK)
	git show $(OLDREV):doomgeneric/i_alsasound.c > $@

audiomod_test: $(AMODSRC)
	$(CC) $(CFLAGS) $(INC) $(AMODFLAGS) -o $@ audiomod_test.c ../i_alsasound.c ../i_alsatick.c ../i_sndmix.c

audiomod_old: audiomod_test.c $(WORK)/i_alsasound_old.c ../i_sndmix.c
	$(CC) $(CFLAGS) $(INC) $(AMODFLAGS) -DOLDMODULE -o $@ audiomod_test.c $(WORK)/i_alsasound_old.c ../i_sndmix.c

audiomod_test_armel: $(AMODSRC)
	arm-linux-gnueabi-gcc $(CFLAGS) $(ARMFLAGS) $(INC) $(AMODFLAGS) -o $@ audiomod_test.c ../i_alsasound.c ../i_alsatick.c ../i_sndmix.c

audiomod_old_armel: audiomod_test.c $(WORK)/i_alsasound_old.c ../i_sndmix.c
	arm-linux-gnueabi-gcc $(CFLAGS) $(ARMFLAGS) $(INC) $(AMODFLAGS) -DOLDMODULE -o $@ audiomod_test.c $(WORK)/i_alsasound_old.c ../i_sndmix.c

musplay_test: $(MUSSRC)
	$(CC) $(CFLAGS) $(INC) -o $@ musplay_test.c

armel: sndmix_test_armel tick_test_armel audiomod_test_armel audiomod_old_armel musplay_test_armel modtest_armel $(WORK)/music.flac $(WORK)/music.raw
	qemu-arm-static ./sndmix_test_armel render-armel.wav $(WAD)
	qemu-arm-static ./tick_test_armel
	qemu-arm-static ./audiomod_test_armel | tee audiomod-armel.out
	qemu-arm-static ./audiomod_old_armel | tee audiomod_old-armel.out
	grep RESULT audiomod-armel.out > audiomod-armel.res; grep RESULT audiomod_old-armel.out | diff - audiomod-armel.res
	diff audiomod.res audiomod-armel.res
	qemu-arm-static ./musplay_test_armel $(WORK)/music.flac $(WORK)/music.raw $(WAD)
	qemu-arm-static ./modtest_armel $(WORK)/mod $(WORK)/lump.bin $(WORK)/music.raw

sndmix_test_armel: sndmix_test.c ../i_sndmix.c ../i_sndmix.h
	arm-linux-gnueabi-gcc $(CFLAGS) $(ARMFLAGS) $(INC) -o $@ sndmix_test.c -lm

musplay_test_armel: $(MUSSRC)
	arm-linux-gnueabi-gcc $(CFLAGS) $(ARMFLAGS) $(INC) -o $@ musplay_test.c

clean:
	rm -rf obj-native obj-armel present_hl present_hl_armel present_pak_test present_pak_test_armel present_bench present_bench_armel present_pak_test_san sndmix_test sndmix_test_armel tick_test tick_test_armel audiomod_test audiomod_test_armel audiomod_old audiomod_old_armel audiomod*.out audiomod*.res musplay_test musplay_test_armel modtest modtest_armel \
	       render.wav render-armel.wav $(WORK)

.PHONY: all armel clean

# The presentation path (dg_present): exactness, damage property, statistics.
#
#   make present        native build and run: present_hl on the Freedoom demo
#                       (also writes the frame pack), then present_pak_test
#                       and present_bench on the pack
#   make present-armel  static armel build, run under qemu-arm-static, -quick
#
# present_hl needs the engine objects, built here from the Makefile.wayland
# list without the Wayland and sound files and without FEATURE_SOUND.
# REF=ref_convert.c (from gen_ref_convert.sh) adds the T4 comparison with the
# converter as it was before dg_present.

PRES_ALL := $(shell sed -n 's/^SRC_DOOM = //p' ../Makefile.wayland)
PRES_OBJS := $(filter-out doomgeneric_wayland.o xdg-shell-protocol.o \
             idle-inhibit-unstable-v1-protocol.o i_alsapcm.o i_alsasound.o \
             i_alsamusic.o i_musindex.o i_musdec_flac.o i_musplay.o i_sndmix.o, \
             $(PRES_ALL))
PRES_FLAGS = -Wall -DNORMALUNIX -DLINUX -D_DEFAULT_SOURCE -DCMAP256 \
             -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -I.. -I.
PRES_NATIVE = -O2 -fsigned-char -fgnu89-inline
PRES_ARMEL = -O3 -fno-pie -no-pie -march=armv5te -mtune=xscale -mfloat-abi=soft \
             -fsigned-char -fgnu89-inline -ffunction-sections -fdata-sections
ARMCC = arm-linux-gnueabi-gcc
WORK ?= work
REF ?=

obj-native/%.o: ../%.c
	@mkdir -p obj-native
	$(CC) $(PRES_NATIVE) $(PRES_FLAGS) -w -c $< -o $@
obj-native/%.o: %.c
	@mkdir -p obj-native
	$(CC) $(PRES_NATIVE) $(PRES_FLAGS) -c $< -o $@
obj-armel/%.o: ../%.c
	@mkdir -p obj-armel
	$(ARMCC) $(PRES_ARMEL) $(PRES_FLAGS) -w -c $< -o $@
obj-armel/%.o: %.c
	@mkdir -p obj-armel
	$(ARMCC) $(PRES_ARMEL) $(PRES_FLAGS) -c $< -o $@

PRES_CHECK = present_check.o dg_present.o
PRES_HL_N = $(addprefix obj-native/,$(PRES_OBJS) present_check.o present_hl.o)
PRES_HL_A = $(addprefix obj-armel/,$(PRES_OBJS) present_check.o present_hl.o)

present_hl: $(PRES_HL_N)
	$(CC) $^ -o $@ -lm
present_hl_armel: $(PRES_HL_A)
	$(ARMCC) -static -no-pie -Wl,--gc-sections $^ -o $@ -lm

PAKSRC = present_pak_test.c present_check.c ../dg_present.c ../dg_present_conv.inc
present_pak_test: $(PAKSRC) pak_read.h present_check.h ../dg_present.h
	$(CC) $(PRES_NATIVE) $(PRES_FLAGS) $(if $(REF),-DWITH_REF) -o $@ $(filter %.c,$(PAKSRC)) $(REF)
present_pak_test_armel: $(PAKSRC) pak_read.h present_check.h ../dg_present.h
	$(ARMCC) $(PRES_ARMEL) -static $(PRES_FLAGS) $(if $(REF),-DWITH_REF) -o $@ $(filter %.c,$(PAKSRC)) $(REF)

# The sanitizers catch what the host's tolerant unaligned access hides (the
# board's ARMv5 core is not tolerant).
present_pak_test_san: $(PAKSRC) pak_read.h present_check.h ../dg_present.h
	$(CC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
	  -fsigned-char $(PRES_FLAGS) -o $@ $(filter %.c,$(PAKSRC))

present_bench: present_bench.c ../dg_present.c ../dg_present_conv.inc ../dg_present.h pak_read.h
	$(CC) $(PRES_NATIVE) $(PRES_FLAGS) -o $@ present_bench.c ../dg_present.c
present_bench_armel: present_bench.c ../dg_present.c ../dg_present_conv.inc ../dg_present.h pak_read.h
	$(ARMCC) $(PRES_ARMEL) -static $(PRES_FLAGS) -o $@ present_bench.c ../dg_present.c

present: present_hl present_pak_test present_pak_test_san present_bench
	mkdir -p $(WORK)
	./present_hl -iwad $(WAD) -timedemo demo1 -nosound \
	  -dumpframes $(WORK)/frames.pak -dumpstep 35 | tee $(WORK)/present_hl.log | tail -80
	grep -q "PRESENT_TEST: PASS" $(WORK)/present_hl.log
	./present_pak_test $(WORK)/frames.pak | tail -20
	./present_pak_test_san $(WORK)/frames.pak | tail -3
	./present_bench -label host -n 200 $(WORK)/frames.pak

present-armel: present_hl_armel present_pak_test_armel present_bench_armel
	mkdir -p $(WORK)
	qemu-arm-static ./present_hl_armel -iwad $(WAD) -timedemo demo1 -nosound \
	  -quick | tee $(WORK)/present_hl_armel.log | tail -40
	grep -q "PRESENT_TEST: PASS" $(WORK)/present_hl_armel.log

.PHONY: present present-armel
