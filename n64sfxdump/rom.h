// ROM loading + endianness normalization.
//
// N64 ROM dumps come in three byte orders, distinguished by the first 4 bytes
// of the header:
//   .z64  0x80 0x37 0x12 0x40  -> 0x80371240  big-endian / native (what we want)
//   .v64  0x37 0x80 0x40 0x12  -> 0x37804012  byte-swapped (each 16-bit pair)
//   .n64  0x40 0x12 0x37 0x80  -> 0x40123780  little-endian (each 32-bit word)
//
// The original tools assumed a .z64 was already provided and only handled .v64
// inconsistently. We normalize any of the three to .z64 up front so every
// downstream reader can assume big-endian.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace n64 {

enum class RomFormat { Z64, V64, N64, Unknown };

struct Rom {
    std::vector<uint8_t> data; // always normalized to .z64 (big-endian) byte order
    RomFormat original = RomFormat::Unknown;
};

// Load a ROM file and normalize to .z64. Returns false and fills `error` on
// failure. If the header magic is unrecognized, the data is left as-is and
// `original` is Unknown (we proceed assuming .z64).
bool load_rom(const std::string& path, Rom& out, std::string& error);

const char* format_name(RomFormat f);

} // namespace n64
