#include "albank.h"

#include "byteio.h"

#include <algorithm>
#include <cstdio>

namespace n64 {

namespace {

// Sanity caps: a false 0x42310001 match in arbitrary ROM data must not send us
// allocating gigabytes or looping forever. Real banks are far below these.
constexpr uint32_t kMaxInstruments = 4096;
constexpr uint32_t kMaxSounds = 4096;
constexpr uint32_t kMaxSampleLen = 16u * 1024 * 1024;
constexpr uint32_t kMaxPredictors = 256;

// Resolve an ALBankFile pointer to an absolute ROM offset. Standard banks store
// file-relative offsets (added to the ctl base); the "flagged" split-ctl forms
// aren't produced by the plain B1 layout, so we only handle the common path.
uint32_t resolve(uint32_t ctl_offset, uint32_t raw) { return ctl_offset + raw; }

// Read an ADPCM predictor book at absolute offset `pred_off`.
bool read_book(const std::vector<uint8_t>& rom, uint32_t pred_off, AdpcmBook& book) {
    uint32_t order, npred;
    if (!try_be32(rom, pred_off + 0x0, order)) return false;
    if (!try_be32(rom, pred_off + 0x4, npred)) return false;
    if (order != 2) return false;                 // N64 codec is always order 2
    if (npred == 0 || npred > kMaxPredictors) return false;
    uint32_t count = order * npred * 8;
    book.order = order;
    book.npredictors = npred;
    book.predictors.resize(count);
    for (uint32_t z = 0; z < count; z++) {
        uint16_t v;
        if (!try_be16(rom, pred_off + 0x8 + z * 2, v)) return false;
        book.predictors[z] = static_cast<int16_t>(v);
    }
    return true;
}

// Read a single wave's metadata (base/len/type + book) from its wavetable.
bool read_wave_meta(const std::vector<uint8_t>& rom, uint32_t ctl_offset,
                    uint32_t wavetable_off, WaveMeta& out) {
    uint32_t base_raw, len;
    uint8_t type;
    if (!try_be32(rom, wavetable_off + 0x0, base_raw) ||
        !try_be32(rom, wavetable_off + 0x4, len) ||
        !try_u8(rom, wavetable_off + 0x8, type))
        return false;
    if (len == 0 || len > kMaxSampleLen) return false;

    out.type = type;
    out.len = len;
    uint8_t flag = (base_raw >> 24) & 0xFF;
    if (flag == 0x40) { // 64DD-IPL bank: sample data is absolute in ROM
        out.is_64dd = true;
        out.base = (base_raw & 0xFFFFFF) + 0x140000;
    } else {
        out.is_64dd = false;
        out.base = base_raw;
    }

    if (type == AL_ADPCM_WAVE) {
        uint32_t pred_ptr;
        if (try_be32(rom, wavetable_off + 0x10, pred_ptr) && pred_ptr != 0) {
            AdpcmBook book;
            if (read_book(rom, resolve(ctl_offset, pred_ptr), book)) {
                out.has_book = true;
                out.book = std::move(book);
            }
        }
    }
    return true;
}

} // namespace

std::vector<WaveMeta> collect_wave_meta(const std::vector<uint8_t>& rom,
                                        uint32_t ctl_offset, uint32_t& header_rate,
                                        uint32_t& ctl_end) {
    std::vector<WaveMeta> waves;
    header_rate = 0;
    uint64_t end = static_cast<uint64_t>(ctl_offset) + 8;
    auto bump = [&](uint64_t e) { if (e > end) end = e; };

    uint32_t bank_ptr;
    if (!try_be32(rom, ctl_offset + 0x4, bank_ptr)) { ctl_end = static_cast<uint32_t>(end); return waves; }
    uint32_t bank_off = resolve(ctl_offset, bank_ptr);

    uint16_t count, samplerate;
    if (!try_be16(rom, bank_off + 0x0, count)) { ctl_end = static_cast<uint32_t>(end); return waves; }
    if (!try_be16(rom, bank_off + 0x6, samplerate)) { ctl_end = static_cast<uint32_t>(end); return waves; }
    if (count > kMaxInstruments) { ctl_end = static_cast<uint32_t>(end); return waves; }
    header_rate = samplerate;
    bump(static_cast<uint64_t>(bank_off) + 0xC + 4ull * count);

    auto walk_instrument = [&](uint32_t inst_off, int inst_index) {
        uint8_t inst_flags;
        uint16_t sound_count;
        if (!try_u8(rom, inst_off + 0x3, inst_flags)) return;
        if (!try_be16(rom, inst_off + 0xE, sound_count)) return;
        if (inst_flags != 0x0) return; // only the plain layout
        if (sound_count == 0 || sound_count > kMaxSounds) return;
        bump(static_cast<uint64_t>(inst_off) + 0x10 + 4ull * sound_count);

        for (uint16_t y = 0; y < sound_count; y++) {
            uint32_t sound_ptr;
            if (!try_be32(rom, inst_off + 0x10 + y * 4, sound_ptr)) continue;
            uint32_t sound_off = resolve(ctl_offset, sound_ptr);
            bump(static_cast<uint64_t>(sound_off) + 0x10);

            uint8_t sound_flags;
            if (!try_u8(rom, sound_off + 0xE, sound_flags)) continue;
            if (sound_flags != 0x0) continue;

            uint32_t env_ptr, keymap_ptr, wavetable_ptr;
            if (try_be32(rom, sound_off + 0x0, env_ptr) && env_ptr != 0)
                bump(static_cast<uint64_t>(resolve(ctl_offset, env_ptr)) + 0x10);
            if (try_be32(rom, sound_off + 0x4, keymap_ptr) && keymap_ptr != 0)
                bump(static_cast<uint64_t>(resolve(ctl_offset, keymap_ptr)) + 0x8);
            if (!try_be32(rom, sound_off + 0x8, wavetable_ptr)) continue;
            uint32_t wavetable_off = resolve(ctl_offset, wavetable_ptr);
            bump(static_cast<uint64_t>(wavetable_off) + 0x14);

            WaveMeta w;
            w.inst_index = inst_index;
            w.sound_index = y;
            if (read_wave_meta(rom, ctl_offset, wavetable_off, w)) {
                if (w.has_book) {
                    uint32_t book_ptr;
                    if (try_be32(rom, wavetable_off + 0x10, book_ptr) && book_ptr)
                        bump(static_cast<uint64_t>(resolve(ctl_offset, book_ptr)) + 0x8 +
                             static_cast<uint64_t>(w.book.order) * w.book.npredictors * 8 * 2);
                }
                waves.push_back(std::move(w));
            }
        }
    };

    // Percussion (optional): pointer at bank+8.
    uint32_t perc_ptr;
    if (try_be32(rom, bank_off + 0x8, perc_ptr) && perc_ptr != 0)
        walk_instrument(resolve(ctl_offset, perc_ptr), -1);

    for (uint16_t x = 0; x < count; x++) {
        uint32_t inst_ptr;
        if (!try_be32(rom, bank_off + 0xC + x * 4, inst_ptr)) continue;
        if (inst_ptr == 0) continue; // empty slot
        walk_instrument(resolve(ctl_offset, inst_ptr), x);
    }
    ctl_end = static_cast<uint32_t>(std::min<uint64_t>(end, rom.size()));
    return waves;
}

ExtractedSound decode_wave_meta(const std::vector<uint8_t>& rom, const WaveMeta& w,
                                uint32_t ctl_offset, uint32_t tbl_offset,
                                int bank_index, uint32_t sample_rate) {
    ExtractedSound s;
    s.ctl_offset = ctl_offset;
    s.bank_index = bank_index;
    s.inst_index = w.inst_index;
    s.sound_index = w.sound_index;
    s.sample_rate = sample_rate;
    s.wave_type = w.type;
    s.tbl_base = w.base;
    s.len = w.len;

    uint32_t sample_off = w.is_64dd ? w.base : (tbl_offset + w.base);
    if (static_cast<uint64_t>(sample_off) + w.len > rom.size()) {
        s.note = "sample data out of range";
        return s;
    }

    if (w.type == AL_ADPCM_WAVE) {
        if (!w.has_book) { s.note = "ADPCM sound has no predictor book"; return s; }
        s.pcm = vadpcm_decode(&rom[sample_off], w.len, w.book);
        s.decoded = !s.pcm.empty();
        if (!s.decoded) s.note = "VADPCM decode produced no samples";
    } else if (w.type == AL_RAW16_WAVE) {
        uint32_t n = w.len / 2;
        s.pcm.resize(n);
        for (uint32_t i = 0; i < n; i++)
            s.pcm[i] = static_cast<int16_t>(read_be16(&rom[sample_off + i * 2]));
        s.decoded = true;
    } else {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "unsupported wave type %u (compressed?)", w.type);
        s.note = buf;
    }
    return s;
}

// ---- .tbl auto-discovery -------------------------------------------------

namespace {

// Does the VADPCM frame stream at absolute `off` obey pred < npred for the
// first `frames` frames? (The hard structural invariant of real VADPCM.)
bool frames_pred_valid(const std::vector<uint8_t>& rom, uint32_t off,
                       uint32_t npred, uint32_t frames) {
    for (uint32_t k = 0; k < frames; k++) {
        size_t p = static_cast<size_t>(off) + 9 * k;
        if (p >= rom.size()) return false;
        if ((rom[p] & 0xF) >= npred) return false;
    }
    return true;
}

} // namespace

bool discover_tbl(const std::vector<uint8_t>& rom, uint32_t ctl_offset,
                  uint32_t ctl_end, const std::vector<WaveMeta>& waves,
                  uint32_t& tbl_offset, double& confidence, std::string& note) {
    // Constraint set: ADPCM waves that live in the .tbl (not absolute 64DD),
    // with a valid book and at least one frame.
    std::vector<const WaveMeta*> con;
    for (const auto& w : waves)
        if (w.type == AL_ADPCM_WAVE && !w.is_64dd && w.has_book && w.len >= 9)
            con.push_back(&w);
    if (con.empty()) { note = "no ADPCM waves to constrain the .tbl"; return false; }

    uint64_t max_end = 0;
    const WaveMeta* anchor = con[0];
    for (const WaveMeta* w : con) {
        max_end = std::max<uint64_t>(max_end, static_cast<uint64_t>(w->base) + w->len);
        if (w->len > anchor->len) anchor = w;
    }
    uint32_t anchor_frames = anchor->len / 9;

    // Stage 1: slide the anchor across the ROM; keep offsets whose first frames
    // obey the predictor invariant. T (the .tbl start) = anchor_off - anchor.base.
    // The .tbl never overlaps the .ctl structure, so skip that range.
    const uint32_t kAnchorFrames = std::min<uint32_t>(anchor_frames, 128);
    std::vector<uint32_t> cands;
    if (max_end <= rom.size()) {
        for (uint64_t T = 0; T + max_end <= rom.size(); T += 1) {
            if (T < ctl_end && T + max_end > ctl_offset) continue; // overlaps the .ctl
            uint32_t anchor_off = static_cast<uint32_t>(T) + anchor->base;
            if ((rom[anchor_off] & 0xF) >= anchor->book.npredictors) continue; // fast reject
            if (frames_pred_valid(rom, anchor_off, anchor->book.npredictors, kAnchorFrames))
                cands.push_back(static_cast<uint32_t>(T));
        }
    }
    if (cands.empty()) { note = "no offset satisfied the anchor invariant"; return false; }

    // Stage 2: joint prune — every constraint wave must obey the invariant over
    // its first frames at the same T.
    const uint32_t kJointFrames = 24;
    std::vector<uint32_t> survivors;
    for (uint32_t T : cands) {
        bool ok = true;
        for (const WaveMeta* w : con) {
            uint32_t nf = std::min(w->len / 9, kJointFrames);
            if (!frames_pred_valid(rom, T + w->base, w->book.npredictors, nf)) { ok = false; break; }
        }
        if (ok) survivors.push_back(T);
    }
    if (survivors.empty()) { note = "no offset satisfied the joint invariant"; return false; }

    // Stage 3: full decode of every constraint wave; require the invariant over
    // ALL frames, reject silence (a run of zero bytes decodes to 0 and trivially
    // "passes"), then rank by aggregate clip fraction (cleanest audio wins).
    const double kEnergyFloor = 16.0; // mean |sample|; real banks aren't silent
    std::vector<std::pair<double, uint32_t>> ranked; // (clip, T)
    bool saw_silence = false;
    for (uint32_t T : survivors) {
        bool all_valid = true;
        size_t total = 0, clipped = 0;
        uint64_t abs_sum = 0;
        for (const WaveMeta* w : con) {
            if (!frames_pred_valid(rom, T + w->base, w->book.npredictors, w->len / 9)) {
                all_valid = false;
                break;
            }
            std::vector<int16_t> pcm = vadpcm_decode(&rom[T + w->base], w->len, w->book);
            total += pcm.size();
            for (int16_t s : pcm) {
                if (s >= 32000 || s <= -32000) clipped++;
                abs_sum += static_cast<uint64_t>(s < 0 ? -s : s);
            }
        }
        if (!all_valid) continue;
        double mean_abs = total ? static_cast<double>(abs_sum) / total : 0.0;
        if (mean_abs < kEnergyFloor) { saw_silence = true; continue; } // reject silence
        ranked.emplace_back(total ? static_cast<double>(clipped) / total : 1.0, T);
    }
    if (ranked.empty()) {
        note = saw_silence ? "only silence-like offsets matched (no real signal)"
                           : "no offset satisfied the full joint constraints";
        return false;
    }
    std::sort(ranked.begin(), ranked.end());

    double best_clip = ranked[0].first;
    tbl_offset = ranked[0].second;

    // Offsets within a few frames of the winner are frame-shift *aliases* of the
    // same .tbl (the byte stream is a run of valid frames, so starting a frame
    // early/late still parses) — not a real ambiguity. Find the closest-clip
    // competitor that is a genuinely *distinct* ROM location.
    const uint32_t kAliasWindow = 256;
    const std::pair<double, uint32_t>* rival = nullptr;
    for (size_t i = 1; i < ranked.size(); i++) {
        uint32_t d = ranked[i].second > tbl_offset ? ranked[i].second - tbl_offset
                                                   : tbl_offset - ranked[i].second;
        if (d > kAliasWindow) { rival = &ranked[i]; break; }
    }

    // Confidence: cleaner audio and more constraints raise it; a distinct rival
    // that decodes nearly as cleanly lowers it.
    double c_clip = std::max(0.0, 1.0 - best_clip / 0.10);
    double c_strength = std::min(1.0, con.size() / 8.0);
    confidence = c_clip * c_strength;
    if (rival && (rival->first - best_clip) < 0.02) {
        confidence *= 0.5;
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "ambiguous: rival .tbl 0x%X clips %.1f%% vs %.1f%%",
                      rival->second, rival->first * 100.0, best_clip * 100.0);
        note = buf;
    }
    return true;
}

std::vector<ExtractedSound> extract_all_sounds(const std::vector<uint8_t>& rom,
                                               uint32_t forced_rate,
                                               std::vector<std::string>& report) {
    std::vector<ExtractedSound> sounds;
    if (rom.size() < 8) return sounds;

    int bank_index = 0;
    for (uint32_t x = 0; x + 4 <= rom.size(); x += 4) {
        if (read_be32(&rom[x]) != 0x42310001u) continue;

        uint32_t header_rate = 0, ctl_end = 0;
        std::vector<WaveMeta> waves = collect_wave_meta(rom, x, header_rate, ctl_end);
        if (waves.empty()) continue;

        uint32_t rate = forced_rate ? forced_rate : (header_rate ? header_rate : 22050);

        uint32_t tbl = 0;
        double conf = 0.0;
        std::string note;
        if (!discover_tbl(rom, x, ctl_end, waves, tbl, conf, note)) {
            char buf[200];
            std::snprintf(buf, sizeof(buf),
                          "ctl @ 0x%06X: %zu wave(s) but .tbl not recovered (%s)",
                          x, waves.size(), note.c_str());
            report.emplace_back(buf);
            continue;
        }

        int decoded = 0;
        for (const auto& w : waves) {
            ExtractedSound s = decode_wave_meta(rom, w, x, tbl, bank_index, rate);
            if (s.decoded) decoded++;
            sounds.push_back(std::move(s));
        }

        char buf[240];
        std::snprintf(buf, sizeof(buf),
                      "bank %d: ctl @ 0x%06X, tbl @ 0x%06X (confidence %.0f%%%s%s) — "
                      "%zu wave(s), %d decoded",
                      bank_index, x, tbl, conf * 100.0,
                      note.empty() ? "" : "; ", note.c_str(), waves.size(), decoded);
        report.emplace_back(buf);
        bank_index++;
    }
    return sounds;
}

} // namespace n64
