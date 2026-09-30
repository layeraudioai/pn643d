# n64sfxdump

Find every sound **sample** in an N64 ROM (the sound effects, gunshots, announcer/voice clips, etc.) and dump each one as a WAV file.

A small, portable command-line tool for Linux (builds with clang or GCC, script-friendly, no GUI). It figures out where a game's sound data lives on its own with no game database.

```
n64sfxdump <rom> <output-dir> [options]
```

Example:
```
./n64sfxdump "BattleTanx (U).z64" /tmp/bt-sfx
```

- `<rom>`: an N64 ROM in `.z64` (big-endian), `.v64` (byte-swapped) or `.n64` (little-endian). The byte order is auto-detected from the header and normalized internally, so any of the three works.
- `<output-dir>`: created if it doesn't exist; one `.wav` is written per sound.

Options:

- `--rate N`: force the output sample rate to `N` Hz (default: the rate stored in each bank, or 22050 if unknown).
- `--verbose`: print each discovered bank, its recovered `.ctl`/`.tbl` offsets, a confidence score, and the reason any sound slot was skipped.
- `-h`, `--help`.

## What it does (and doesn't)

N64 audio uses libultra's **ALBankFile** format: a `.ctl` (structure metadata: banks → instruments → sounds → wavetables) paired with a `.tbl` (raw sample bytes). The sound effects and voice clips are the *samples* in these banks. Music is stored separately as sequence (MIDI-like) data and is intentionally **not** touched by this tool.

`n64sfxdump` scans the ROM for standard ALBankFiles (magic `0x42310001`), walks each bank to its individual sounds, decodes the sample data (Nintendo **VADPCM**, which is th usual codec, or raw **PCM16**) and writes standard mono 16-bit RIFF/WAVE files.

### Finding the `.tbl` without a database

The `.ctl` and `.tbl` are separate blobs stored at unrelated ROM offsets (in BattleTanx, the `.tbl` sits ~1 MB *before* its `.ctl`), so the `.tbl` can't be found by scanning for a header because it has none. Instead, n64sfxdump **recovers** it: the `.ctl` fully describes every sample (its offset *within* the `.tbl`, its length, and its ADPCM predictor book), and VADPCM has a hard structural invariant (each 9-byte frame's predictor index must be `< npredictors`). So the tool solves for the single ROM offset at which *all* of a bank's waves simultaneously decode as valid, non-silent audio. Because a bank has dozens of independent waves, that offset is over-determined and comes out essentially unique. `--verbose` reports a confidence score per bank.

### Coverage

Works today on games that store **standard, uncompressed** ALBank soundbanks, no configuration, no database. Not yet handled:

- Games that store their audio **compressed** (MusyX, RNC, zlib-wrapped, MORT speech, etc). The sample bytes aren't sitting in the ROM in decodable form, so they need that game's decompressor first.
- Uncompressed banks in **non-standard layouts** (pointer-table variants) that don't use the standard `0x42310001` ALBankFile header, which need a layout-specific parser.

Run with `--verbose` to see what was found and skipped.

### No MP3

Output is WAV. To transcode, pipe the results through `ffmpeg`, e.g. `for f in *.wav; do ffmpeg -i "$f" "${f%.wav}.mp3"; done`.

## Output naming

```
bank<NN>_ctl<CTLOFFSET>_i<III>_s<SS>.wav      # instrument sound
bank<NN>_ctl<CTLOFFSET>_perc_s<SS>.wav        # percussion sound
```

where `NN` is the bank index, `CTLOFFSET` is the ROM offset of the bank, `III` the instrument index, and `SS` the sound index within the instrument.

## Building

Requires CMake ≥ 3.16 and a C++17 compiler. clang is preferred automatically when present; otherwise GCC is used.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
# binary: build/n64sfxdump
```

## Testing

```
./test/run_tests.sh
```

This builds a synthetic ROM containing a known soundbank (three VADPCM waves at distinct `.tbl` offsets with all-zero predictor books, so the decoded output is exactly predictable, plus one raw PCM16 wave), padded with `0xFF` so a wrong offset fails the VADPCM invariant. It runs `n64sfxdump` against `.z64`/`.v64`/ `.n64` variants and checks that discovery recovers the correct `.tbl` and the decoded WAV samples match hand-computed expected values.

## How it works

`n64sfxdump` normalizes the ROM to big-endian, scans for ALBankFile headers, walks the `bank → instrument → sound → wavetable` structure to each sample, recovers the `.tbl` offset by the over-determined VADPCM constraint described above (excluding the `.ctl` region and rejecting silence), decodes each sample from VADPCM (9-byte frames → 16 PCM samples, order-2 predictor book) or raw PCM16, and writes mono 16-bit RIFF/WAVE.
