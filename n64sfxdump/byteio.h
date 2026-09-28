// Big-endian byte-buffer readers.
//
// N64 data (once a ROM is normalized to .z64) stores multi-byte fields
// big-endian. The original N64SoundListTool read every field through
// helpers named CharArrayToLong/CharArrayToShort that assemble bytes MSB
// first; these are the portable equivalents, with bounds checking added so a
// stray magic-byte match while scanning an arbitrary ROM cannot read out of
// range.
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace n64 {

inline uint16_t read_be16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

inline uint32_t read_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           (static_cast<uint32_t>(p[3]));
}

// Bounds-checked variants over a whole buffer. Return true and write the value
// only when the full field lies inside [0, size). Used everywhere the offset is
// derived from untrusted ROM contents.
inline bool try_be16(const std::vector<uint8_t>& buf, size_t off, uint16_t& out) {
    if (off + 2 > buf.size()) return false;
    out = read_be16(&buf[off]);
    return true;
}

inline bool try_be32(const std::vector<uint8_t>& buf, size_t off, uint32_t& out) {
    if (off + 4 > buf.size()) return false;
    out = read_be32(&buf[off]);
    return true;
}

inline bool try_u8(const std::vector<uint8_t>& buf, size_t off, uint8_t& out) {
    if (off + 1 > buf.size()) return false;
    out = buf[off];
    return true;
}

} // namespace n64
