#include "stdafx.h"
#include "CTRPerfLearning.h"

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
}
}
