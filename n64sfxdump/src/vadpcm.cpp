#include "vadpcm.h"

#include <cstring>

namespace n64 {

// 4-bit nibble -> signed value (two's complement over 4 bits).
static const int16_t itable[16] = {
    0, 1, 2, 3, 4, 5, 6, 7,
    -8, -7, -6, -5, -4, -3, -2, -1,
};

static int16_t sign_extend(unsigned bits, int x) {
    int m = 1 << (bits - 1);
    x = x & ((1 << bits) - 1);
    return static_cast<int16_t>((x ^ m) - m);
}

// Decode one 8-sample sub-block. Mirrors decode_8 (N64AIFCAudio.cpp:20783).
// `pred1` points at 16 shorts: the two 8-wide predictor vectors (pred1, pred2).
static void decode_8(const uint8_t* in, int16_t* out, int index,
                     const int16_t* pred1, int16_t lastsmp[8]) {
    int16_t tmp[8];
    std::memset(out, 0, sizeof(int16_t) * 8);

    const int16_t* pred2 = pred1 + 8;

    for (int i = 0; i < 8; i++) {
        int nib = (i & 1) ? (in[i >> 1] & 0xf) : ((in[i >> 1] >> 4) & 0xf);
        tmp[i] = static_cast<int16_t>(itable[nib] << index);
        tmp[i] = sign_extend(index + 4, tmp[i]);
    }

    for (int i = 0; i < 8; i++) {
        long total = static_cast<long>(pred1[i]) * lastsmp[6];
        total += static_cast<long>(pred2[i]) * lastsmp[7];

        if (i > 0) {
            for (int x = i - 1; x > -1; x--) {
                total += static_cast<long>(tmp[(i - 1) - x]) * pred2[x];
            }
        }

        // Q11 fixed point, matching the reference exactly.
        int result = (static_cast<int>(tmp[i] << 0xb) + static_cast<int>(total)) >> 0xb;
        int16_t sample;
        if (result > 32767) sample = 32767;
        else if (result < -32768) sample = -32768;
        else sample = static_cast<int16_t>(result);
        out[i] = sample;
    }

    std::memcpy(lastsmp, out, sizeof(int16_t) * 8);
}

std::vector<int16_t> vadpcm_decode(const uint8_t* in, size_t len, const AdpcmBook& book) {
    std::vector<int16_t> out;
    if (book.npredictors == 0 || book.order == 0) return out;
    // The N64 codec is always order 2; a decode_8 sub-block reads 16 predictor
    // shorts (order*8). Guard the book so a corrupt header can't over-read.
    if (book.order != 2) return out;
    size_t need = static_cast<size_t>(book.order) * book.npredictors * 8;
    if (book.predictors.size() < need) return out;

    int16_t lastsmp[8];
    for (int i = 0; i < 8; i++) lastsmp[i] = 0;

    // Whole 9-byte frames only.
    size_t frames = len / 9;
    out.reserve(frames * 16);

    const uint8_t* p = in;
    for (size_t frame = 0; frame < frames; frame++) {
        int index = (*p >> 4) & 0xf;
        int pred = (*p & 0xf);
        pred = pred % static_cast<int>(book.npredictors);
        p++;

        const int16_t* pred1 = &book.predictors[static_cast<size_t>(pred) * 16];

        int16_t block[16];
        decode_8(p, &block[0], index, pred1, lastsmp);
        p += 4;
        decode_8(p, &block[8], index, pred1, lastsmp);
        p += 4;

        for (int i = 0; i < 16; i++) out.push_back(block[i]);
    }

    return out;
}

} // namespace n64
