// Host-runnable regression test for the CTR per-game profiler and tuner.
#include "SysCTR/Utility/CTRPerfLearning.h"
#include "SysCTR/Utility/CTRStorage.h"
#include "Utility/Timing.h"
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

static std::map<std::string, std::vector<unsigned char> > gFiles;
static u64 gNow = 0;
namespace CTRStorage {
bool ExtDataEnsure(u64, u64) { return true; }
bool ExtDataRead(u64, const char *name, void *data, size_t size, size_t *actual) {
    std::map<std::string, std::vector<unsigned char> >::iterator it = gFiles.find(name);
    if (it == gFiles.end()) return false;
    size_t count = it->second.size() < size ? it->second.size() : size;
    memcpy(data, it->second.data(), count);
    if (actual) *actual = count;
    return true;
}
bool ExtDataWrite(u64, const char *name, const void *data, size_t size) {
    const unsigned char *bytes = static_cast<const unsigned char *>(data);
    gFiles[name] = std::vector<unsigned char>(bytes, bytes + size);
    return true;
}
}
namespace NTiming {
bool GetPreciseFrequency(u64 *frequency) { *frequency = 1000000; return true; }
bool GetPreciseTime(u64 *time) { *time = gNow; return true; }
u64 ToMilliseconds(u64 ticks) { return ticks / 1000; }
}

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main() {
    CTRPerfLearning::BeginGame(0x12345678, 0xABCDEF01, 'E', 4 * 1024 * 1024);
    CHECK(CTRPerfLearning::GetRecommendedBackendCeilingFPS(60) == 60);
    for (unsigned i = 0; i < 120; ++i) {
        CTRPerfLearning::RecordFrame(10000, 16667); // 40% measured headroom
        gNow += 100;
        { CTRPerfLearning::CScopedZone zone(CTRPerfLearning::PROFILE_CPU_VBL); gNow += 25; }
    }
    CHECK(CTRPerfLearning::GetFrameSamples() == 120);
    CHECK(CTRPerfLearning::GetRecommendedBackendCeilingFPS(60) == 120);
    CTRPerfLearning::SZoneStats stats = {};
    CHECK(CTRPerfLearning::GetZoneStats(CTRPerfLearning::PROFILE_CPU_VBL, &stats));
    CHECK(stats.calls == 120 && stats.max_ticks == 25 && stats.total_ticks == 3000);
    CTRPerfLearning::EndGame();
    CHECK(gFiles.size() == 2); // binary profile and human-readable report
    bool report_found = false;
    for (std::map<std::string, std::vector<unsigned char> >::const_iterator it = gFiles.begin(); it != gFiles.end(); ++it) {
        if (it->first.find(".txt") != std::string::npos) {
            std::string report(it->second.begin(), it->second.end());
            report_found = report.find("frame samples: 120") != std::string::npos &&
                report.find("CPU_VBL calls=120") != std::string::npos;
        }
    }
    CHECK(report_found);

    // No learned settings leak between titles.
    CTRPerfLearning::BeginGame(0x99887766, 0x01020304, 'J', 2 * 1024 * 1024);
    CHECK(CTRPerfLearning::GetFrameSamples() == 0);
    for (unsigned i = 0; i < 120; ++i)
        CTRPerfLearning::RecordFrame(20000, 16667); // overloaded: no aggressive cadence
    CHECK(CTRPerfLearning::GetRecommendedBackendCeilingFPS(60) == 60);
    CTRPerfLearning::EndGame();
    CHECK(gFiles.size() == 4);

    // The first title reloads its persisted data and its recommendation.
    CTRPerfLearning::BeginGame(0x12345678, 0xABCDEF01, 'E', 4 * 1024 * 1024);
    CHECK(CTRPerfLearning::GetFrameSamples() == 120);
    CHECK(CTRPerfLearning::GetRecommendedBackendCeilingFPS(60) == 120);
    CHECK(CTRPerfLearning::GetRecommendedBackendCeilingFPS(200) == 240);
    CTRPerfLearning::EndGame();
    std::puts("CTR profiler/tuner tests passed (per-game persistence, isolation, zones, tuning)");
    return 0;
}
