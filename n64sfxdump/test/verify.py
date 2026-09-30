#!/usr/bin/env python3
"""Verify n64sfxdump discovery output against the fixture's expected samples."""
import struct, sys, wave, os, glob

def read_wav(path):
    with wave.open(path, "rb") as w:
        assert w.getsampwidth() == 2 and w.getnchannels() == 1
        n = w.getnframes()
        return list(struct.unpack("<%dh" % n, w.readframes(n)))

def main():
    out_dir, exp_path = sys.argv[1], sys.argv[2]
    expected = []   # list of (label, samples)
    with open(exp_path) as f:
        for line in f:
            p = line.split()
            if p[0] in ("ADPCM", "RAW16"):
                label = p[0] + (p[1] if p[0] == "ADPCM" else "")
                nums = [int(x) for x in p[(2 if p[0] == "ADPCM" else 1):]]
                expected.append((label, nums))

    got = [read_wav(f) for f in sorted(glob.glob(os.path.join(out_dir, "*.wav")))]
    print("found %d wav(s)" % len(got))
    ok = True
    for label, samples in expected:
        if samples in got:
            print("PASS: %s matches (%d samples)" % (label, len(samples)))
        else:
            print("FAIL: %s not found (expected %d samples)" % (label, len(samples)))
            ok = False
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
