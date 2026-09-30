// n64sfxdump — find every sound sample (SFX / voice clips) in an N64 ROM and
// dump each one as a WAV file.
//
// Usage: n64sfxdump <rom> <output-dir> [options]
//
// It scans the ROM for standard libultra soundbanks (the .ctl/.tbl ALBankFile
// format), recovers each bank's .tbl offset with no catalog — by solving for
// the offset at which every wave decodes as valid VADPCM — then decodes each
// sample (VADPCM or raw PCM16) to a mono 16-bit WAV. Music (sequence data) is
// a different structure and is intentionally not touched.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "albank.h"
#include "rom.h"
#include "wav.h"

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kDefaultSampleRate = 22050;

void print_usage(const char* argv0) {
    std::fprintf(stderr,
        "n64sfxdump — dump N64 sound effects to WAV\n"
        "\n"
        "Usage: %s <rom> <output-dir> [options]\n"
        "\n"
        "  <rom>          N64 ROM (.z64, .v64 or .n64 — auto byte-swapped)\n"
        "  <output-dir>   directory to write .wav files into (created if needed)\n"
        "\n"
        "Options:\n"
        "  --rate N       force output sample rate to N Hz (default: per-bank, "
        "or %u if unknown)\n"
        "  --verbose      print per-bank discovery details and skip reasons\n"
        "  -h, --help     show this help\n",
        argv0, kDefaultSampleRate);
}

} // namespace

int main(int argc, char** argv) {
    uint32_t forced_rate = 0;
    bool verbose = false;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") { print_usage(argv[0]); return 0; }
        else if (a == "--verbose") verbose = true;
        else if (a == "--rate") {
            if (i + 1 >= argc) { std::fprintf(stderr, "error: --rate needs a value\n"); return 2; }
            forced_rate = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (a.rfind("--", 0) == 0) {
            std::fprintf(stderr, "error: unknown option '%s'\n", a.c_str());
            print_usage(argv[0]); return 2;
        } else positional.push_back(a);
    }

    if (positional.size() != 2) { print_usage(argv[0]); return 2; }
    const std::string& rom_path = positional[0];
    const std::string& out_dir = positional[1];

    n64::Rom rom;
    std::string error;
    if (!n64::load_rom(rom_path, rom, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    std::printf("Loaded %s (%zu bytes, %s)\n", rom_path.c_str(), rom.data.size(),
                n64::format_name(rom.original));
    if (rom.original == n64::RomFormat::Unknown)
        std::fprintf(stderr, "warning: unrecognized ROM header; assuming .z64 byte order\n");

    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
        std::fprintf(stderr, "error: cannot create output directory '%s': %s\n",
                     out_dir.c_str(), ec.message().c_str());
        return 1;
    }

    std::vector<std::string> report;
    std::vector<n64::ExtractedSound> sounds =
        n64::extract_all_sounds(rom.data, forced_rate, report);

    if (verbose)
        for (const auto& line : report) std::printf("  %s\n", line.c_str());

    int written = 0, skipped = 0;
    for (const auto& s : sounds) {
        if (!s.decoded) {
            skipped++;
            if (verbose)
                std::printf("  skip b%02d i%03d s%02d @0x%06X: %s\n", s.bank_index,
                            s.inst_index, s.sound_index, s.ctl_offset,
                            s.note.empty() ? "not decoded" : s.note.c_str());
            continue;
        }
        uint32_t rate = s.sample_rate ? s.sample_rate : kDefaultSampleRate;
        if (rate < 2000 || rate > 192000) rate = kDefaultSampleRate;

        char name[128];
        if (s.inst_index < 0)
            std::snprintf(name, sizeof(name), "bank%02d_ctl%06X_perc_s%02d.wav",
                          s.bank_index, s.ctl_offset, s.sound_index);
        else
            std::snprintf(name, sizeof(name), "bank%02d_ctl%06X_i%03d_s%02d.wav",
                          s.bank_index, s.ctl_offset, s.inst_index, s.sound_index);
        fs::path outfile = fs::path(out_dir) / name;

        std::string werr;
        if (!n64::write_wav_mono_pcm16(outfile.string(), rate, s.pcm, werr)) {
            std::fprintf(stderr, "error: %s\n", werr.c_str());
            continue;
        }
        written++;
    }

    std::printf("Done: %d WAV file(s) written to %s (%d slot(s) skipped)\n",
                written, out_dir.c_str(), skipped);
    if (written == 0)
        std::printf("No sounds extracted. This ROM may store audio compressed or in\n"
                    "a game-specific format the scanner doesn't handle. Try --verbose.\n");
    return 0;
}
