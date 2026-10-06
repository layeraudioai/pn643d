#pragma once

#include "Utility/DaedalusTypes.h"

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
// Returns the recent EWMA emulation-work / frame-budget ratio as a percentage.
// Returns false until enough samples exist to make a stable recommendation.
bool GetWorkloadFitness(u32 *workload_percent);
// Temporary, volatile measurement window used by the in-game auto-optimizer.
// The first 30 frames are discarded to let each candidate settle.
void BeginFitnessProbe();
bool EndFitnessProbe(u32 *workload_percent);

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
