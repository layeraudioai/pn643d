#pragma once

#include "Utility/DaedalusTypes.h"

<<<<<<< HEAD
namespace CTRPerfLearning {

enum EProfileZone {
    PROFILE_CPU_VBL = 0,
    PROFILE_FRAME_LIMITER,
    PROFILE_ZONE_COUNT
};

// Profiles are keyed by the full N64 ROM identity and persisted in CTR extdata.
void BeginGame(u32 crc1, u32 crc2, u8 country, u32 rom_size);
void EndGame();
void RecordFrame(u32 work_ticks, u32 target_ticks);
void RecordZone(EProfileZone zone, u32 elapsed_ticks);
u32 GetRecommendedBackendCeilingFPS(u32 user_target_fps);

struct SZoneStats {
    u64 total_ticks;
    u32 calls;
    u32 max_ticks;
};
bool GetZoneStats(EProfileZone zone, SZoneStats *stats);
u32 GetFrameSamples();
bool WriteReport(u32 user_target_fps);

class CScopedZone {
public:
    explicit CScopedZone(EProfileZone zone);
    ~CScopedZone();
private:
    EProfileZone mZone;
    u64 mStarted;
    bool mValid;
};

}

#define CTR_PERF_SCOPE(zone) CTRPerfLearning::CScopedZone ctr_perf_scope_##__LINE__(zone)
=======
namespace CTRPerfLearning
{
// Start a per-cartridge profile session using the stable N64 ROM identity.
void BeginGame(u32 crc1, u32 crc2, u8 country, u32 romSize);
void EndGame();

// Applies a conservative learned policy. New 3DS uses the existing full-ROM
// path when size permits; Old 3DS only tries it after measured cache I/O stalls.
bool ShouldPreloadROM(u32 romSize, bool isNew3DS);

void RecordRomLoad(bool preloadAttempted, bool fullRomLoaded,
                   bool succeeded, u64 elapsedTicks);
void RecordCacheRead(u32 bytes, u64 elapsedTicks, bool succeeded);
void RecordFrame(u64 elapsedTicks, u64 targetFrameTicks);
}
>>>>>>> 9b15d14f43462799691a3b08bffae82f9bd0d18d
