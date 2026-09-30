#include "rom.h"

#include "byteio.h"

#include <cstdio>

namespace n64 {

const char* format_name(RomFormat f) {
    switch (f) {
        case RomFormat::Z64: return "z64 (big-endian)";
        case RomFormat::V64: return "v64 (byte-swapped)";
        case RomFormat::N64: return "n64 (little-endian)";
        default: return "unknown";
    }
}

static void swap16_in_place(std::vector<uint8_t>& d) {
    // Swap each 16-bit pair: v64 -> z64.
    size_t n = d.size() & ~static_cast<size_t>(1);
    for (size_t i = 0; i < n; i += 2) {
        std::swap(d[i], d[i + 1]);
    }
}

static void swap32_in_place(std::vector<uint8_t>& d) {
    // Reverse each 32-bit word: n64 -> z64.
    size_t n = d.size() & ~static_cast<size_t>(3);
    for (size_t i = 0; i < n; i += 4) {
        std::swap(d[i], d[i + 3]);
        std::swap(d[i + 1], d[i + 2]);
    }
}

bool load_rom(const std::string& path, Rom& out, std::string& error) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        error = "cannot open ROM file: " + path;
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    if (size <= 0) {
        std::fclose(f);
        error = "ROM file is empty or unreadable: " + path;
        return false;
    }
    std::fseek(f, 0, SEEK_SET);
    out.data.resize(static_cast<size_t>(size));
    size_t got = std::fread(out.data.data(), 1, out.data.size(), f);
    std::fclose(f);
    if (got != out.data.size()) {
        error = "short read on ROM file: " + path;
        return false;
    }

    if (out.data.size() < 4) {
        error = "ROM too small to have a header";
        return false;
    }

    uint32_t magic = read_be32(&out.data[0]);
    switch (magic) {
        case 0x80371240u:
            out.original = RomFormat::Z64; // already correct
            break;
        case 0x37804012u:
            out.original = RomFormat::V64;
            swap16_in_place(out.data);
            break;
        case 0x40123780u:
            out.original = RomFormat::N64;
            swap32_in_place(out.data);
            break;
        default:
            out.original = RomFormat::Unknown; // assume z64, proceed
            break;
    }
    return true;
}

} // namespace n64
