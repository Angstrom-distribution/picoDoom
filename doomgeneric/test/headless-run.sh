#!/bin/bash
# headless-run.sh BINARY NAME RUNSECS "CHEATS" [doom arguments]
#
# Runs the headless harness under qemu-arm-static with Freedoom. The WAV of
# what the fake device played is NAME.wav, the log is NAME.log (both in
# $OUTDIR, default the current directory). The harness quits after RUNSECS
# seconds, or earlier when -timedemo ends. WAD defaults to /out/freedoom1.wad.
bin=$1; n=$2; export RUNSECS=$3 CHEATS=$4; shift 4
OUTDIR=${OUTDIR:-$PWD}
WAD=${WAD:-/out/freedoom1.wad}
mkdir -p /tmp/qrun-$n && cd /tmp/qrun-$n
export PCMWAV=$OUTDIR/$n.wav HOME=/tmp/qrun-$n
( time timeout 600 qemu-arm-static "$bin" -iwad "$WAD" "$@" ) > $OUTDIR/$n.log 2>&1
echo "exit $?" >> $OUTDIR/$n.log
