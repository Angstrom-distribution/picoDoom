# Writes a synthetic 22050 Hz mono 16-bit WAV for musplay_test: three
# detuned tones, a clipped burst and noise, 100001 frames (not a multiple of
# the decoder chunk or of the FLAC block size). No WAD data is used.
import math, random, struct, sys, wave

N = 100001
random.seed(7)
frames = []
for i in range(N):
    t = i / 22050.0
    v = 9000 * math.sin(2 * math.pi * 220 * t) + 6000 * math.sin(2 * math.pi * 331.3 * t)
    v += 3000 * math.sin(2 * math.pi * (880 + 200 * math.sin(t)) * t)
    if 30000 <= i < 31000:
        v *= 3.0
    if 60000 <= i < 62000:
        v += random.randint(-12000, 12000)
    if 80000 <= i < 80500:
        v = 0
    frames.append(max(-32768, min(32767, int(v))))
w = wave.open(sys.argv[1], "wb")
w.setnchannels(1)
w.setsampwidth(2)
w.setframerate(22050)
w.writeframes(struct.pack("<%dh" % N, *frames))
w.close()

# Optional second argument: a directory for modtest. It gets a fake music
# lump (lump.bin, pseudo-random bytes), and mod/index.txt that maps the FNV-1a
# of that lump to d_e1m1.flac. The caller copies the FLAC next to the index.
if len(sys.argv) > 2:
    import os
    d = sys.argv[2]
    os.makedirs(os.path.join(d, "mod"), exist_ok=True)
    lump = bytes(random.randrange(256) for _ in range(150000))
    open(os.path.join(d, "lump.bin"), "wb").write(lump)
    h = 0x811c9dc5
    for b in lump:
        h = ((h ^ b) * 0x01000193) & 0xffffffff
    open(os.path.join(d, "mod", "index.txt"), "w").write("# test\n%08x d_e1m1.flac\n" % h)
