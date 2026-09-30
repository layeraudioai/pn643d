// Minimal RIFF/WAVE PCM16 writer.
//
// Ported from N64SoundListTool's GenerateWavPCMHeader (N64AIFCAudio.cpp:22073),
// which hand-writes the 0x2C-byte header little-endian with no Windows structs.
// We keep it standalone and portable.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace n64 {

// Write a mono 16-bit PCM WAV. `samples` are host-order signed 16-bit samples.
// Returns false on file-open failure.
bool write_wav_mono_pcm16(const std::string& path,
                          uint32_t sample_rate,
                          const std::vector<int16_t>& samples,
                          std::string& error);

} // namespace n64
