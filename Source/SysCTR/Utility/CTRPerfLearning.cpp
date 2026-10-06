#include "stdafx.h"
#include "CTRPerfLearning.h"
#include "CTRStorage.h"
#include "Utility/Timing.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

namespace CTRPerfLearning {
namespace {
const u32 kProfileVersion = 1;
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
bool sDirty = false;
u32 sAppIdLow = 0;
u32 sLastTargetFPS = 0;
bool sFitnessProbeActive = false;
u32 sFitnessProbeWarmup = 0;
u32 sFitnessProbeSamples = 0;
u64 sFitnessProbeWork = 0;
u64 sFitnessProbeTarget = 0;

void Persist() {
    if (sActive)
        CTRStorage::ExtDataWrite(sAppIdLow, sProfileFile, &sProfile, sizeof(sProfile));
    sDirty = false;
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
    snprintf(sProfileFile, sizeof(sProfileFile), "perf-%08" PRIX32 "%08" PRIX32 "-%02X.dat",
        (uint32_t)crc1, (uint32_t)crc2, (unsigned)country);
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
    sDirty = false;
    sLastTargetFPS = 0;
    sActive = true;
}

void EndGame() {
    if (sActive) {
        if (sDirty)
            Persist();
        WriteReport(sLastTargetFPS);
    }
    sActive = false;
}

void RecordFrame(u32 work_ticks, u32 target_ticks) {
    if (!sActive || target_ticks == 0)
        return;
    if (sFitnessProbeActive) {
        if (sFitnessProbeWarmup != 0) {
            --sFitnessProbeWarmup;
        } else {
            sFitnessProbeWork += work_ticks;
            sFitnessProbeTarget += target_ticks;
            ++sFitnessProbeSamples;
        }
    }
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
    // Do not write extdata from the per-frame path: the archive flush blocks
    // emulation and was causing a visible hitch about once per second. Save
    // the accumulated profile once, when the ROM is closed.
    sDirty = true;
}

void RecordZone(EProfileZone zone, u32 elapsed_ticks) {
    if (!sActive || zone < 0 || zone >= PROFILE_ZONE_COUNT)
        return;
    SZoneStats &stats = sProfile.zones[zone];
    stats.total_ticks += elapsed_ticks;
    ++stats.calls;
    if (elapsed_ticks > stats.max_ticks)
        stats.max_ticks = elapsed_ticks;
    // Do not write extdata from the per-frame path: the archive flush blocks
    // emulation and was causing a visible hitch about once per second. Save
    // the accumulated profile once, when the ROM is closed.
    sDirty = true;
}

bool GetWorkloadFitness(u32 *workload_percent) {
    if (!sActive || !workload_percent ||
        sProfile.frame_samples < kMinimumSamplesForTuning ||
        sProfile.average_target_ticks_x100 == 0)
        return false;

    // The EWMA is scaled by 100 on both operands, so the ratio is unchanged.
    // Saturate rather than allowing a pathological profile to wrap.
    const u64 percent = (sProfile.average_work_ticks_x100 * 100u) /
        sProfile.average_target_ticks_x100;
    *workload_percent = percent > 1000u ? 1000u : (u32)percent;
    return true;
}

void BeginFitnessProbe() {
    sFitnessProbeActive = true;
    sFitnessProbeWarmup = 30;
    sFitnessProbeSamples = 0;
    sFitnessProbeWork = 0;
    sFitnessProbeTarget = 0;
}

bool EndFitnessProbe(u32 *workload_percent) {
    sFitnessProbeActive = false;
    if (!workload_percent || sFitnessProbeSamples < 30 || sFitnessProbeTarget == 0)
        return false;
    const u64 percent = (sFitnessProbeWork * 100u) / sFitnessProbeTarget;
    *workload_percent = percent > 1000u ? 1000u : (u32)percent;
    return true;
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
    snprintf(reportName, sizeof(reportName), "perf-%08" PRIX32 "%08" PRIX32 "-%02X.txt",
        (uint32_t)sProfile.crc1, (uint32_t)sProfile.crc2, (unsigned)sProfile.country);
    char report[512];
    int used = snprintf(report, sizeof(report),
        "DaedalusX64 per-game performance profile\n"
        "ROM CRC: %08" PRIX32 " %08" PRIX32 "  country: %02X  size: %" PRIu32 "\n"
        "frame samples: %" PRIu32 "  average work/target: %llu/%llu ticks\n"
        "backend ceiling recommendation: %" PRIu32 " FPS (user cap remains unchanged)\n",
        (uint32_t)sProfile.crc1, (uint32_t)sProfile.crc2, (unsigned)sProfile.country,
        (uint32_t)sProfile.rom_size, (uint32_t)sProfile.frame_samples,
        (unsigned long long)(sProfile.average_work_ticks_x100 / 100u),
        (unsigned long long)(sProfile.average_target_ticks_x100 / 100u),
        (uint32_t)GetRecommendedBackendCeilingFPS(user_target_fps));
    if (used < 0 || (size_t)used >= sizeof(report))
        return false;
    for (u32 i = 0; i < PROFILE_ZONE_COUNT; ++i) {
        const SZoneStats &zone = sProfile.zones[i];
        const char *name = i == PROFILE_CPU_VBL ? "CPU_VBL" : "FRAME_LIMITER";
        int written = snprintf(report + used, sizeof(report) - (size_t)used,
            "%s calls=%" PRIu32 " total=%llu max=%" PRIu32 "\n", name,
            (uint32_t)zone.calls, (unsigned long long)zone.total_ticks, (uint32_t)zone.max_ticks);
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
