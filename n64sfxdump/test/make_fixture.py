#!/usr/bin/env python3
"""Generate a synthetic N64 ROM with one standard ALBankFile soundbank, to test
n64sfxdump's catalog-free .tbl auto-discovery end to end.

The bank has three VADPCM waves (at distinct .tbl bases) plus one raw PCM16
wave. The ADPCM waves use an all-zero predictor book with npredictors=4, so:
  * decoded output is exactly the sign-extended, scale-shifted nibbles
    (predictor terms vanish) — hand-verifiable, and
  * the predictor-index invariant is "low nibble < 4".
The ROM is filled with 0xFF everywhere else, so any frame-shifted / wrong .tbl
offset hits 0xFF (low nibble 0xF >= 4) and fails the invariant — leaving the
true .tbl as the unique solution discovery must find.
"""
import struct, sys

def be16(v): return struct.pack(">H", v & 0xFFFF)
def be32(v): return struct.pack(">I", v & 0xFFFFFFFF)

INDEX = 4                       # VADPCM scale (larger -> louder, above silence floor)
NPRED = 4
ITABLE = [0,1,2,3,4,5,6,7,-8,-7,-6,-5,-4,-3,-2,-1]

def se(bits, x):
    m = 1 << (bits-1); x &= (1 << bits)-1; return (x ^ m) - m

# ---- ctl layout (offsets relative to ctl start) ----
BANK=0x10; INST=0x20
SND=[0x40,0x50,0x60,0x70]
ENV=0x80; KEYMAP=0x90
WT=[0xA0,0xC0,0xE0,0x100]       # wavetables
BOOK=0x120
# ---- tbl layout (offsets relative to tbl start) ----
BASE=[0x00,0x40,0x80]           # three ADPCM waves
RAWBASE=0x100
NFRAMES=4

def make_frames(seed):
    """Deterministic distinct nibble stream -> (bytes, expected_samples).
    Each frame is 9 bytes: 1 header + 8 data bytes (16 nibbles / 16 samples)."""
    data=bytearray(); expected=[]
    for f in range(NFRAMES):
        data.append((INDEX<<4) | 0x00)          # header: pred=0
        databytes=[]
        for k in range(8):
            n_hi=(seed + f*8 + k*2) % 16
            n_lo=(seed + f*8 + k*2 + 1) % 16
            databytes.append((n_hi<<4)|n_lo)
        data.extend(databytes)
        for b in databytes:
            for nib in ((b>>4)&0xF, b&0xF):
                expected.append(se(INDEX+4, ITABLE[nib] << INDEX))
    return bytes(data), expected

def build_ctl():
    b=bytearray(0x1B0)
    def put(o,d): b[o:o+len(d)]=d
    put(0x00, be16(0x4231)); put(0x02, be16(1)); put(0x04, be32(BANK))
    # bank
    put(BANK+0x00, be16(1)); put(BANK+0x06, be16(22050)); put(BANK+0x0C, be32(INST))
    # instrument: flags @+3 = 0, soundCount @+0xE = 4, sound ptrs @+0x10
    put(INST+0x00, bytes([0x7F,0x40,0x05,0x00]))
    put(INST+0x0E, be16(4))
    for i,s in enumerate(SND): put(INST+0x10+i*4, be32(s))
    # sounds
    for i,s in enumerate(SND):
        put(s+0x00, be32(ENV)); put(s+0x04, be32(KEYMAP)); put(s+0x08, be32(WT[i]))
        put(s+0x0C, bytes([0x40,0x7F,0x00]))
    put(ENV+0x00, be32(1000))
    put(KEYMAP+0x00, bytes([0,127,0,127,60,0]))
    # wavetables: 3 ADPCM + 1 RAW16
    for i in range(3):
        put(WT[i]+0x00, be32(BASE[i])); put(WT[i]+0x04, be32(NFRAMES*9))
        put(WT[i]+0x08, bytes([0x00,0x00])); put(WT[i]+0x0C, be32(0)); put(WT[i]+0x10, be32(BOOK))
    put(WT[3]+0x00, be32(RAWBASE)); put(WT[3]+0x04, be32(8)); put(WT[3]+0x08, bytes([0x01,0x00]))
    # book: order 2, npred 4, all-zero predictors
    put(BOOK+0x00, be32(2)); put(BOOK+0x04, be32(NPRED))
    return bytes(b)

def main():
    out_rom = sys.argv[1] if len(sys.argv)>1 else "fixture.z64"
    exp_path = sys.argv[2] if len(sys.argv)>2 else "expected.txt"

    ctl=build_ctl()
    adpcm=[make_frames(seed) for seed in (1,5,11)]
    raw=[1000,-1000,32767,-32768]

    rom=bytearray(b"\xFF"*0x4000)      # 0xFF fill kills frame-shift aliases
    rom[0x00:0x04]=be32(0x80371240)
    rom[0x20:0x24]=b"TEST"
    CTL=0x1000; TBL=0x2000
    rom[CTL:CTL+len(ctl)]=ctl
    for i in range(3):
        rom[TBL+BASE[i]:TBL+BASE[i]+len(adpcm[i][0])]=adpcm[i][0]
    for i,s in enumerate(raw):
        rom[TBL+RAWBASE+i*2:TBL+RAWBASE+i*2+2]=struct.pack(">h",s)

    open(out_rom,"wb").write(rom)
    with open(exp_path,"w") as f:
        for i in range(3):
            f.write("ADPCM %d %s\n" % (i, " ".join(str(v) for v in adpcm[i][1])))
        f.write("RAW16 %s\n" % " ".join(str(v) for v in raw))
        f.write("TBL 0x%X\n" % TBL)
    print("wrote %s (%d bytes), ctl@0x%X tbl@0x%X" % (out_rom, len(rom), CTL, TBL))

if __name__=="__main__":
    main()
