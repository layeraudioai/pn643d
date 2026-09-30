// Nintendo VADPCM decoder (the codec libultra uses for N64 sound samples).
//
// Ported from N64SoundListTool's CN64AIFCAudio::decode / decode_8 / SignExtend
// (N64AIFCAudio.cpp:20771-21097, "By Ice Mario!"). Pure buffer math, no OS deps.
//
// Format: 9-byte frames -> 16 samples. The first byte of a frame is
// (scale << 4) | predictorIndex; the next 8 bytes hold 16 signed 4-bit nibbles.
// Each frame decodes as two 8-sample sub-blocks against a 2-vector predictor
// selected from the ADPCM "book".
#pragma once

#include <cstdint>
#include <vector>

namespace n64 {

struct AdpcmBook {
    uint32_t order = 0;
    uint32_t npredictors = 0;
    std::vector<int16_t> predictors; // order * npredictors * 8 entries
};

// Decode `len` bytes of VADPCM from `in` using `book`. Returns the decoded
// signed-16 PCM samples. Safe against malformed books (returns empty).
std::vector<int16_t> vadpcm_decode(const uint8_t* in, size_t len, const AdpcmBook& book);

} // namespace n64
