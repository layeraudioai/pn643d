#include "wav.h"

#include <cstdio>

namespace n64 {

static void put_le16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(v & 0xFF);
    b.push_back((v >> 8) & 0xFF);
}

static void put_le32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(v & 0xFF);
    b.push_back((v >> 8) & 0xFF);
    b.push_back((v >> 16) & 0xFF);
    b.push_back((v >> 24) & 0xFF);
}

static void put_tag(std::vector<uint8_t>& b, const char* t) {
    b.push_back(static_cast<uint8_t>(t[0]));
    b.push_back(static_cast<uint8_t>(t[1]));
    b.push_back(static_cast<uint8_t>(t[2]));
    b.push_back(static_cast<uint8_t>(t[3]));
}

bool write_wav_mono_pcm16(const std::string& path,
                          uint32_t sample_rate,
                          const std::vector<int16_t>& samples,
                          std::string& error) {
    const uint16_t channels = 1;
    const uint16_t bits = 16;
    const uint16_t block_align = channels * (bits / 8);
    const uint32_t byte_rate = sample_rate * block_align;
    const uint32_t data_size = static_cast<uint32_t>(samples.size()) * block_align;

    std::vector<uint8_t> hdr;
    hdr.reserve(0x2C);
    put_tag(hdr, "RIFF");
    put_le32(hdr, 0x24 + data_size); // RIFF chunk size = 4 + (8+16) + (8+data)
    put_tag(hdr, "WAVE");
    put_tag(hdr, "fmt ");
    put_le32(hdr, 16);           // fmt subchunk size
    put_le16(hdr, 1);            // audio format = PCM
    put_le16(hdr, channels);
    put_le32(hdr, sample_rate);
    put_le32(hdr, byte_rate);
    put_le16(hdr, block_align);
    put_le16(hdr, bits);
    put_tag(hdr, "data");
    put_le32(hdr, data_size);

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        error = "cannot open output WAV for writing: " + path;
        return false;
    }
    std::fwrite(hdr.data(), 1, hdr.size(), f);

    // Samples are little-endian in a WAV file.
    for (int16_t s : samples) {
        uint16_t u = static_cast<uint16_t>(s);
        uint8_t bytes[2] = {static_cast<uint8_t>(u & 0xFF),
                            static_cast<uint8_t>((u >> 8) & 0xFF)};
        std::fwrite(bytes, 1, 2, f);
    }
    std::fclose(f);
    return true;
}

} // namespace n64
