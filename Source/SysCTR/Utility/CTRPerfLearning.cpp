#include "stdafx.h"
#include "CTRPerfLearning.h"
<<<<<<< HEAD
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
=======

#include "SysCTR/Utility/CTRStorage.h"
#include "Utility/Timing.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>

namespace CTRPerfLearning
{
namespace
{
static const u32 kProfileMagic = 0x5046524D; // "PFRM"
static const u32 kTableMagic = 0x50465442;   // "PFTB"
static const u16 kProfileVersion = 1;
static const u16 kTableVersion = 1;
static const u8 kFlagPreloadRecommended = 0x01;
static const u32 kPreloadMinROMSize = 8 * 1024 * 1024;
static const u32 kCacheReadCountThreshold = 128;
static const u16 kMaxGameProfiles = 512;
static const char kProfileTableFilename[] = "perf_profiles.bin";

struct __attribute__((packed)) ProfileDisk
{
    u32 magic;
    u16 version;
    u16 size;
    u32 crc1;
    u32 crc2;
    u8 country;
    u8 flags;
    u16 reserved;
    u32 lastUsed;
    u32 sessions;
    u32 frames;
    u32 lateFrames;
    u32 cacheReads;
    u32 cacheBytes;
    u32 fullROMLoads;
    u32 preloadFailures;
    u64 frameTicks;
    u64 cacheReadTicks;
    u64 fullROMLoadTicks;
    u32 checksum;
};

struct __attribute__((packed)) ProfileTableDisk
{
    u32 magic;
    u16 version;
    u16 count;
    u32 size;
    u32 generation;
    u32 checksum;
    ProfileDisk profiles[kMaxGameProfiles];
};

static ProfileTableDisk s_table;
static ProfileDisk *s_profile = NULL;
static u64 s_extDataId = 0;
static u64 s_timerFrequency = 0;
static u64 s_sessionCacheTicks = 0;
static u32 s_sessionCacheReads = 0;
static u32 s_romSize = 0;
static bool s_tableLoaded = false;
static bool s_active = false;
static bool s_preloadAttempted = false;
static bool s_fullROMLoaded = false;
static bool s_preloadFailed = false;

static u32 Checksum(const void *data, size_t size)
{
    const u8 *bytes = static_cast<const u8 *>(data);
    u32 hash = 2166136261u;
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

static u32 SaturatingAdd(u32 value, u32 amount)
{
    return (value > 0xFFFFFFFFu - amount) ? 0xFFFFFFFFu : value + amount;
}

static u64 SaturatingAdd(u64 value, u64 amount)
{
    return (value > ~(u64)0 - amount) ? ~(u64)0 : value + amount;
}

static bool IsProfileValid(ProfileDisk &profile)
{
    const u32 expected = profile.checksum;
    profile.checksum = 0;
    const bool valid = profile.magic == kProfileMagic &&
        profile.version == kProfileVersion && profile.size == sizeof(ProfileDisk) &&
        Checksum(&profile, sizeof(profile)) == expected;
    profile.checksum = expected;
    return valid;
}

static u64 GetExtDataId()
{
    u64 programId = 0;
    if (R_FAILED(APT_GetProgramID(&programId)))
        return 0;
    return (programId & 0xFFFFFFFFULL) >> 8;
}

static void LoadProfileTable()
{
    if (s_tableLoaded)
        return;

    memset(&s_table, 0, sizeof(s_table));
    s_extDataId = GetExtDataId();
    CTRStorage::ExtDataEnsure(s_extDataId);

    size_t actual = 0;
    if (CTRStorage::ExtDataRead(s_extDataId, kProfileTableFilename,
            &s_table, sizeof(s_table), &actual) && actual == sizeof(s_table))
    {
        const u32 expected = s_table.checksum;
        s_table.checksum = 0;
        const bool valid = s_table.magic == kTableMagic &&
            s_table.version == kTableVersion && s_table.size == sizeof(s_table) &&
            s_table.count <= kMaxGameProfiles && Checksum(&s_table, sizeof(s_table)) == expected;
        s_table.checksum = expected;
        if (!valid)
            memset(&s_table, 0, sizeof(s_table));
    }
    else
    {
        memset(&s_table, 0, sizeof(s_table));
    }

    s_tableLoaded = true;
}

static ProfileDisk *FindProfile(u32 crc1, u32 crc2, u8 country)
{
    for (u16 i = 0; i < s_table.count; ++i)
    {
        ProfileDisk &profile = s_table.profiles[i];
        if (IsProfileValid(profile) && profile.crc1 == crc1 &&
            profile.crc2 == crc2 && profile.country == country)
            return &profile;
    }
    return NULL;
}

static ProfileDisk *AllocateProfile()
{
    if (s_table.count < kMaxGameProfiles)
        return &s_table.profiles[s_table.count++];

    // Bounded least-recently-used replacement keeps the extdata table small
    // even when users rotate through large ROM collections.
    u16 oldest = 0;
    for (u16 i = 1; i < kMaxGameProfiles; ++i)
        if (s_table.profiles[i].lastUsed < s_table.profiles[oldest].lastUsed)
            oldest = i;
    return &s_table.profiles[oldest];
}

static bool WriteProfileTable()
{
    s_table.magic = kTableMagic;
    s_table.version = kTableVersion;
    s_table.size = sizeof(s_table);
    s_table.checksum = 0;
    s_table.checksum = Checksum(&s_table, sizeof(s_table));
    return CTRStorage::ExtDataWrite(s_extDataId, kProfileTableFilename,
        &s_table, sizeof(s_table));
}
}

void BeginGame(u32 crc1, u32 crc2, u8 country, u32 romSize)
{
    if (s_active)
        EndGame();
    LoadProfileTable();

    s_romSize = romSize;
    s_sessionCacheTicks = 0;
    s_sessionCacheReads = 0;
    s_preloadAttempted = false;
    s_fullROMLoaded = false;
    s_preloadFailed = false;

    s_profile = FindProfile(crc1, crc2, country);
    if (s_profile == NULL)
    {
        s_profile = AllocateProfile();
        memset(s_profile, 0, sizeof(*s_profile));
        s_profile->magic = kProfileMagic;
        s_profile->version = kProfileVersion;
        s_profile->size = sizeof(*s_profile);
        s_profile->crc1 = crc1;
        s_profile->crc2 = crc2;
        s_profile->country = country;
    }

    if (++s_table.generation == 0)
        s_table.generation = 1;
    s_profile->lastUsed = s_table.generation;
    s_profile->sessions = SaturatingAdd(s_profile->sessions, 1);

    s_timerFrequency = 0;
    NTiming::GetPreciseFrequency(&s_timerFrequency);
    s_active = true;
}

bool ShouldPreloadROM(u32 romSize, bool isNew3DS)
{
    if (!s_active || romSize == 0)
        return false;
    if (isNew3DS && romSize <= 32 * 1024 * 1024)
        return true;
    return !isNew3DS && romSize <= kPreloadMinROMSize &&
        (s_profile->flags & kFlagPreloadRecommended) != 0;
}

void RecordRomLoad(bool preloadAttempted, bool fullRomLoaded,
                   bool succeeded, u64 elapsedTicks)
{
    if (!s_active)
        return;
    s_preloadAttempted = preloadAttempted;
    s_fullROMLoaded = fullRomLoaded;
    s_preloadFailed = preloadAttempted && (!succeeded || !fullRomLoaded);
    if (fullRomLoaded)
    {
        s_profile->fullROMLoads = SaturatingAdd(s_profile->fullROMLoads, 1);
        s_profile->fullROMLoadTicks = SaturatingAdd(s_profile->fullROMLoadTicks, elapsedTicks);
    }
    if (s_preloadFailed)
        s_profile->preloadFailures = SaturatingAdd(s_profile->preloadFailures, 1);
}

void RecordCacheRead(u32 bytes, u64 elapsedTicks, bool succeeded)
{
    if (!s_active)
        return;
    s_profile->cacheReads = SaturatingAdd(s_profile->cacheReads, 1);
    s_profile->cacheBytes = SaturatingAdd(s_profile->cacheBytes, bytes);
    s_profile->cacheReadTicks = SaturatingAdd(s_profile->cacheReadTicks, elapsedTicks);
    s_sessionCacheReads = SaturatingAdd(s_sessionCacheReads, 1);
    s_sessionCacheTicks = SaturatingAdd(s_sessionCacheTicks, elapsedTicks);
    if (!succeeded)
        s_preloadFailed = true;
}

void RecordFrame(u64 elapsedTicks, u64 targetFrameTicks)
{
    if (!s_active)
        return;
    s_profile->frames = SaturatingAdd(s_profile->frames, 1);
    s_profile->frameTicks = SaturatingAdd(s_profile->frameTicks, elapsedTicks);
    if (targetFrameTicks != 0 && elapsedTicks > targetFrameTicks + targetFrameTicks / 50)
        s_profile->lateFrames = SaturatingAdd(s_profile->lateFrames, 1);
}

void EndGame()
{
    if (!s_active || s_profile == NULL)
        return;

    // Learn from measured cache churn, rather than speculative ROM-title rules.
    // The recommendation is limited to small ROMs and can be reverted whenever
    // the full-load allocation or source read fails.
    if (s_preloadFailed)
        s_profile->flags &= (u8)~kFlagPreloadRecommended;
    else if (!s_preloadAttempted && s_romSize <= kPreloadMinROMSize &&
             s_sessionCacheReads >= kCacheReadCountThreshold && s_timerFrequency != 0 &&
             s_sessionCacheTicks >= s_timerFrequency / 10)
        s_profile->flags |= kFlagPreloadRecommended;

    s_profile->checksum = 0;
    s_profile->checksum = Checksum(s_profile, sizeof(*s_profile));
    WriteProfileTable();
    s_active = false;
    s_profile = NULL;
>>>>>>> 9b15d14f43462799691a3b08bffae82f9bd0d18d
}
}
