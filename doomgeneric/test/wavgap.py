# wavgap.py FILE.wav [MIN_MS]
# Lists the runs of exact silence (both channels 0) of at least MIN_MS
# (default 150) in a 22050 Hz stereo 16-bit WAV, as start/length in ms, and
# the number of non-silent frames. Used to look at the gap a stall leaves.
import struct, sys, wave

w = wave.open(sys.argv[1], "rb")
rate = w.getframerate()
n = w.getnframes()
data = w.readframes(n)
min_ms = float(sys.argv[2]) if len(sys.argv) > 2 else 150
vals = struct.unpack("<%dh" % (2 * n), data)
runs = []
start = None
nonzero = 0
for i in range(n):
    z = vals[2 * i] == 0 and vals[2 * i + 1] == 0
    nonzero += not z
    if z and start is None:
        start = i
    elif not z and start is not None:
        if (i - start) * 1000.0 / rate >= min_ms:
            runs.append((start, i - start))
        start = None
if start is not None and (n - start) * 1000.0 / rate >= min_ms:
    runs.append((start, n - start))
print("%s: %d frames (%.2f s), %d non-silent" % (sys.argv[1], n, n / rate, nonzero))
for s, l in runs:
    print("  silence from %.3f s for %.1f ms" % (s / rate, l * 1000.0 / rate))
print("  %d silent runs of at least %g ms" % (len(runs), min_ms))
