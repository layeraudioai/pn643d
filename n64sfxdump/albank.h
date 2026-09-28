// libultra ALBankFile discovery + parsing.
//
// An N64 soundbank is a .ctl (structure: banks -> instruments -> sounds ->
// wavetables) paired with a .tbl (raw sample bytes) stored at an unrelated ROM
// offset. We find each .ctl by its "B1" magic, parse its wave list, then
// *recover* the .tbl offset with no catalog by solving for the single offset at
// which every wave decodes as valid VADPCM (the .ctl over-determines the .tbl).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vadpcm.h"

namespace n64 {

// libultra AL wave types (subset we recognize). type 0 = VADPCM (the norm for
// SFX), type 1 = raw PCM16.
enum {
    AL_ADPCM_WAVE = 0,
    AL_RAW16_WAVE = 1,
};

// Metadata for one sound's wave, read from the .ctl (no .tbl needed yet).
struct WaveMeta {
    int inst_index = 0;    // instrument within the bank (-1 = percussion)
    int sound_index = 0;
    uint32_t base = 0;     // offset into the .tbl (or absolute ROM for 64DD)
    uint32_t len = 0;      // sample byte length
    uint8_t type = 0;      // AL_ADPCM_WAVE / AL_RAW16_WAVE / ...
    bool is_64dd = false;  // sample data is absolute in ROM, not in the .tbl
    bool has_book = false;
    AdpcmBook book;
};

// One extracted sound (a single sample slot in a bank).
struct ExtractedSound {
    uint32_t ctl_offset = 0;
    int bank_index = 0;
    int inst_index = 0;        // -1 = percussion
    int sound_index = 0;
    uint32_t sample_rate = 0;
    uint8_t wave_type = 0;
    uint32_t tbl_base = 0;
    uint32_t len = 0;
    bool decoded = false;
    std::vector<int16_t> pcm;
    std::string note;
};

// Collect every wave's metadata for the standard-format bank whose ALBankFile
// magic is at `ctl_offset`. Also returns the bank header's sample rate and the
// end offset of the .ctl structure (so the .tbl search can exclude it). Empty
// if the header is unreadable.
std::vector<WaveMeta> collect_wave_meta(const std::vector<uint8_t>& rom,
                                        uint32_t ctl_offset, uint32_t& header_rate,
                                        uint32_t& ctl_end);

// Recover the .tbl ROM offset for a bank, catalog-free, by finding the single
// offset (outside the .ctl range [ctl_offset, ctl_end)) at which all ADPCM waves
// jointly satisfy the VADPCM predictor-index invariant and decode to non-silent,
// clean audio. Returns true and sets `tbl_offset` + `confidence` (0..1); `note`
// explains failures/ambiguity.
bool discover_tbl(const std::vector<uint8_t>& rom, uint32_t ctl_offset,
                  uint32_t ctl_end, const std::vector<WaveMeta>& waves,
                  uint32_t& tbl_offset, double& confidence, std::string& note);

// Decode one wave (given the resolved .tbl offset) into an ExtractedSound.
ExtractedSound decode_wave_meta(const std::vector<uint8_t>& rom, const WaveMeta& w,
                                uint32_t ctl_offset, uint32_t tbl_offset,
                                int bank_index, uint32_t sample_rate);

// Scan the whole (already .z64-normalized) ROM for standard ALBankFiles,
// auto-discover each one's .tbl, and extract every sound. `report` receives
// human-readable per-bank diagnostics.
std::vector<ExtractedSound> extract_all_sounds(const std::vector<uint8_t>& rom,
                                               uint32_t forced_rate,
                                               std::vector<std::string>& report);

} // namespace n64
