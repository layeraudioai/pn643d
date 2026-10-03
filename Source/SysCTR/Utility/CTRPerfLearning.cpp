#include "stdafx.h"
#include "CTRPerfLearning.h"
#include "CTRStorage.h"
#include "Utility/Timing.h"
#include <stdio.h>
#include <string.h>

namespace CTRPerfLearning {
namespace {
const u32 kProfileVersion = 1;
const u32 kPersistEveryFrames = 120;
const u32 kMinimumSamplesForTuning = 120;
const u32 kMaximumBackendFPS = 240;
char sProfileFile[40] = "perf-profile-v1.dat";

struct SStoredProfile {
    u32 version;
    u32 crc1;
    u32 crc2;
    u32 rom_size;
    u8 country;
    u8 reserved[3];
    u32 frame_samples;
    u64 average_work_ticks_x100;
    u64 average_target_ticks_x100;
    SZoneStats zones[PROFILE_ZONE_COUNT];
};

SStoredProfile sProfile;
bool sActive = false;
u32 sDirtyFrames = 0;
u32 sAppIdLow = 0;
u32 sLastTargetFPS = 0;

void Persist() {
    if (sActive)
        CTRStorage::ExtDataWrite(sAppIdLow, sProfileFile, &sProfile, sizeof(sProfile));
    sDirtyFrames = 0;
}
}

void BeginGame(u32 crc1, u32 crc2, u8 country, u32 rom_size) {
    EndGame();
    memset(&sProfile, 0, sizeof(sProfile));
    sProfile.version = kProfileVersion;
    sProfile.crc1 = crc1;
    sProfile.crc2 = crc2;
    sProfile.country = country;
    sProfile.rom_size = rom_size;
    // The storage backend has a single application extdata namespace; use a
    // collision-resistant per-ROM file so profiles never overwrite each other.
    snprintf(sProfileFile, sizeof(sProfileFile), "perf-%08X%08X-%02X.dat",
        crc1, crc2, (unsigned)country);
    sAppIdLow = 0;
    CTRStorage::ExtDataEnsure(sAppIdLow);
    size_t bytes = 0;
    SStoredProfile saved;
    if (CTRStorage::ExtDataRead(sAppIdLow, sProfileFile, &saved, sizeof(saved), &bytes) &&
        bytes == sizeof(saved) && saved.version == kProfileVersion &&
        saved.crc1 == crc1 && saved.crc2 == crc2 && saved.country == country) {
        sProfile = saved;
        // ROM size can vary for overdumps; the header identity is authoritative.
        sProfile.rom_size = rom_size;
    }
    sDirtyFrames = 0;
    sLastTargetFPS = 0;
    sActive = true;
}

void EndGame() {
    if (sActive) {
        if (sDirtyFrames != 0)
            Persist();
        WriteReport(sLastTargetFPS);
    }
    sActive = false;
}

void RecordFrame(u32 work_ticks, u32 target_ticks) {
    if (!sActive || target_ticks == 0)
        return;
    ++sProfile.frame_samples;
    // Scaled integer EWMAs avoid floating point in the hot path and on ARM.
    const u64 work = (u64)work_ticks * 100u;
    const u64 target = (u64)target_ticks * 100u;
    if (sProfile.frame_samples == 1) {
        sProfile.average_work_ticks_x100 = work;
        sProfile.average_target_ticks_x100 = target;
    } else {
        sProfile.average_work_ticks_x100 = (sProfile.average_work_ticks_x100 * 7u + work) / 8u;
        sProfile.average_target_ticks_x100 = (sProfile.average_target_ticks_x100 * 7u + target) / 8u;
    }
    ++sDirtyFrames;
    if (sDirtyFrames >= kPersistEveryFrames)
        Persist();
}

void RecordZone(EProfileZone zone, u32 elapsed_ticks) {
    if (!sActive || zone < 0 || zone >= PROFILE_ZONE_COUNT)
        return;
    SZoneStats &stats = sProfile.zones[zone];
    stats.total_ticks += elapsed_ticks;
    ++stats.calls;
    if (elapsed_ticks > stats.max_ticks)
        stats.max_ticks = elapsed_ticks;
    ++sDirtyFrames;
    if (sDirtyFrames >= kPersistEveryFrames)
        Persist();
}

u32 GetRecommendedBackendCeilingFPS(u32 user_target_fps) {
    if (user_target_fps != 0) sLastTargetFPS = user_target_fps;
    if (user_target_fps == 0)
        return 0;
    if (!sActive || sProfile.frame_samples < kMinimumSamplesForTuning ||
        sProfile.average_work_ticks_x100 == 0 || sProfile.average_target_ticks_x100 == 0)
        return user_target_fps;

    // Increase background VI cadence only with a measured >=20% headroom.
    // Never changes the user-visible cap; the limiter remains responsible for it.
    if (sProfile.average_work_ticks_x100 * 5u < sProfile.average_target_ticks_x100 * 4u) {
        u32 ceiling = user_target_fps > kMaximumBackendFPS / 2u ?
            kMaximumBackendFPS : user_target_fps * 2u;
        return ceiling < user_target_fps ? user_target_fps : ceiling;
    }
    return user_target_fps;
}

bool GetZoneStats(EProfileZone zone, SZoneStats *stats) {
    if (!sActive || !stats || zone < 0 || zone >= PROFILE_ZONE_COUNT)
        return false;
    *stats = sProfile.zones[zone];
    return true;
}

u32 GetFrameSamples() {
    return sActive ? sProfile.frame_samples : 0;
}

bool WriteReport(u32 user_target_fps) {
    if (!sActive)
        return false;
    char reportName[40];
    snprintf(reportName, sizeof(reportName), "perf-%08X%08X-%02X.txt",
        sProfile.crc1, sProfile.crc2, (unsigned)sProfile.country);
    char report[512];
    int used = snprintf(report, sizeof(report),
        "DaedalusX64 per-game performance profile\n"
        "ROM CRC: %08X %08X  country: %02X  size: %u\n"
        "frame samples: %u  average work/target: %llu/%llu ticks\n"
        "backend ceiling recommendation: %u FPS (user cap remains unchanged)\n",
        sProfile.crc1, sProfile.crc2, (unsigned)sProfile.country, sProfile.rom_size,
        sProfile.frame_samples,
        (unsigned long long)(sProfile.average_work_ticks_x100 / 100u),
        (unsigned long long)(sProfile.average_target_ticks_x100 / 100u),
        GetRecommendedBackendCeilingFPS(user_target_fps));
    if (used < 0 || (size_t)used >= sizeof(report))
        return false;
    for (u32 i = 0; i < PROFILE_ZONE_COUNT; ++i) {
        const SZoneStats &zone = sProfile.zones[i];
        const char *name = i == PROFILE_CPU_VBL ? "CPU_VBL" : "FRAME_LIMITER";
        int written = snprintf(report + used, sizeof(report) - (size_t)used,
            "%s calls=%u total=%llu max=%u\n", name, zone.calls,
            (unsigned long long)zone.total_ticks, zone.max_ticks);
        if (written < 0 || (size_t)written >= sizeof(report) - (size_t)used)
            return false;
        used += written;
    }
    return CTRStorage::ExtDataWrite(sAppIdLow, reportName, report, (size_t)used);
}

CScopedZone::CScopedZone(EProfileZone zone) : mZone(zone), mStarted(0), mValid(false) {
    mValid = NTiming::GetPreciseTime(&mStarted);
}

CScopedZone::~CScopedZone() {
    if (!mValid)
        return;
    u64 ended = 0;
    if (NTiming::GetPreciseTime(&ended) && ended >= mStarted) {
        u64 elapsed = ended - mStarted;
        RecordZone(mZone, elapsed > 0xFFFFFFFFu ? 0xFFFFFFFFu : (u32)elapsed);
    }
}
}
