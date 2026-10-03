/*
Copyright (C) 2007 StrmnNrmn

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "stdafx.h"
#include "FramerateLimiter.h"

#include "Utility/Timing.h"
#include "Utility/Thread.h"

#include "Core/Memory.h"
#include "Core/ROM.h"
#ifdef DAEDALUS_CTR
#include "SysCTR/Utility/CTRPerfLearning.h"
#endif

#include <stdint.h>

static u64 gTicksBetweenVbls = 0;
static u64 gTicksPerSecond = 0;
static long double gTicksPerMillisecond = 0.0L;
static long double gTicksPerFrame = 0.0L;
static long double gFramePeriodFraction = 0.0L;
static u64 gNextFrameDeadline = 0;
static u64 gSleepLeadTicks = 0;
static u64 gLastVITime = 0;
static u32 gLastOrigin = 0;
static u32 gVblsSinceFlip = 0;
static u64 gCurrentAverageTicksPerVbl = 0;
static FramerateSyncFn gAuxSyncFn = NULL;
static void *gAuxSyncArg = NULL;
static f32 gConfiguredTargetFPS = 0.0f;
static u32 gConfiguredSpeedSync = 0;

static u64 gVblSamples[4] = { 0, 0, 0, 0 };
static u32 gVblSampleCount = 0;
static u32 gVblSampleIndex = 0;

static const u32 gTvFrequencies[] = { 240, 240, 240 };
extern float gMaxFPS;

static f32 GetConfiguredTargetFPS()
{
    u32 tv_type = g_ROM.TvType;
    if (tv_type >= sizeof(gTvFrequencies) / sizeof(gTvFrequencies[0]))
        tv_type = 0;

    f32 target = (gMaxFPS > 0.0f) ? gMaxFPS : (f32)gTvFrequencies[tv_type];
    if (target < 1.0f) target = 1.0f;
    if (target > 1000.0f) target = 1000.0f;
    return target;
}

static void ConfigureFramePeriod(f32 target_fps, u32 speed_sync)
{
    gConfiguredTargetFPS = target_fps;
    gConfiguredSpeedSync = speed_sync;
    const long double effective_fps = (long double)target_fps /
        ((speed_sync == 2) ? 2.0L : 1.0L);
    gTicksPerFrame = (gTicksPerSecond != 0 && effective_fps > 0.0L)
        ? (long double)gTicksPerSecond / effective_fps : 0.0L;
    gTicksBetweenVbls = (gTicksPerFrame >= 1.0L)
        ? (u64)gTicksPerFrame : 0;
    gFramePeriodFraction = 0.0L;
    gNextFrameDeadline = 0;
}

static u64 NextFramePeriodTicks()
{
    const long double exact_period = gTicksPerFrame + gFramePeriodFraction;
    u64 whole_ticks = (u64)exact_period;
    gFramePeriodFraction = exact_period - (long double)whole_ticks;
    if (whole_ticks == 0)
        whole_ticks = 1;
    return whole_ticks;
}

static u64 FramerateLimiter_UpdateAverageTicksPerVbl(u64 elapsed_ticks)
{
    gVblSamples[gVblSampleIndex] = elapsed_ticks;
    gVblSampleIndex = (gVblSampleIndex + 1) & 3;
    if (gVblSampleCount < 4)
        ++gVblSampleCount;

    u64 total = 0;
    for (u32 i = 0; i < gVblSampleCount; ++i)
        total += gVblSamples[i];
    return (total + gVblSampleCount / 2) / gVblSampleCount;
}

static u64 WaitUntilDeadline(u64 deadline)
{
    u64 now = 0;
    NTiming::GetPreciseTime(&now);
    if (now >= deadline)
        return now;

    // Learn how early to wake from the OS sleep so scheduler overshoot is
    // compensated. The final short interval is busy-waited against the same
    // high-resolution clock; this prevents millisecond sleep granularity from
    // quantizing the selected FPS.
    const u64 requested_wake = (deadline > gSleepLeadTicks)
        ? deadline - gSleepLeadTicks : now;
    if (requested_wake > now)
    {
        u64 sleep_ticks = requested_wake - now;
        while (sleep_ticks != 0)
        {
            const u32 chunk = (sleep_ticks > 0xFFFFFFFFu)
                ? 0xFFFFFFFFu : (u32)sleep_ticks;
            ThreadSleepTicks(chunk);
            sleep_ticks -= chunk;
        }

        NTiming::GetPreciseTime(&now);
        const u64 overshoot = (now > requested_wake) ? now - requested_wake : 0;
        const u64 minimum_lead = (u64)(gTicksPerMillisecond * 0.10L);
        const u64 maximum_lead = (u64)(gTicksPerMillisecond * 2.0L);
        u64 learned_lead = (gSleepLeadTicks * 7 + overshoot) / 8 + minimum_lead;
        if (learned_lead < minimum_lead) learned_lead = minimum_lead;
        if (maximum_lead != 0 && learned_lead > maximum_lead)
            learned_lead = maximum_lead;
        gSleepLeadTicks = learned_lead;
    }

    while (now < deadline)
        NTiming::GetPreciseTime(&now);
    return now;
}

void FramerateLimiter_SetAuxillarySyncFunction(FramerateSyncFn fn, void *arg)
{
    gAuxSyncFn = fn;
    gAuxSyncArg = arg;
}

bool FramerateLimiter_Reset()
{
    gLastVITime = 0;
    gLastOrigin = 0;
    gVblsSinceFlip = 0;
    gCurrentAverageTicksPerVbl = 0;
    gVblSampleCount = 0;
    gVblSampleIndex = 0;
    gSleepLeadTicks = 0;

    if (!NTiming::GetPreciseFrequency(&gTicksPerSecond) || gTicksPerSecond == 0)
    {
        gTicksPerSecond = 0;
        gTicksPerMillisecond = 0.0L;
        gTicksPerFrame = 0.0L;
        gTicksBetweenVbls = 0;
        gNextFrameDeadline = 0;
        return true;
    }

    // Re-evaluate from the active timer frequency and current user target.
    // This is also refreshed in Limit() when preferences change mid-ROM.
    gTicksPerMillisecond = (long double)gTicksPerSecond / 1000.0L;
    // Start with a modest wake-up lead, then adapt it from measured scheduler
    // overshoot. This avoids an inaccurate first interval without spinning for
    // a large fixed fraction of every frame.
    gSleepLeadTicks = (u64)(gTicksPerMillisecond * 0.25L);
    ConfigureFramePeriod(GetConfiguredTargetFPS(), gSpeedSyncEnabled);
    return true;
}

void FramerateLimiter_Limit()
{
    ++gVblsSinceFlip;

    const u32 current_origin = Memory_VI_GetRegister(VI_ORIGIN_REG);
    if (gAuxSyncFn)
        gAuxSyncFn(gAuxSyncArg);

    if (current_origin == gLastOrigin)
        return;

    u64 now = 0;
    NTiming::GetPreciseTime(&now);

    if (gLastVITime != 0 && gVblsSinceFlip != 0)
    {
        const u64 elapsed_ticks = now - gLastVITime;
        gCurrentAverageTicksPerVbl = FramerateLimiter_UpdateAverageTicksPerVbl(
            elapsed_ticks / gVblsSinceFlip);
    }

    const f32 target_fps = GetConfiguredTargetFPS();
    if (gTicksPerSecond != 0 &&
        (target_fps != gConfiguredTargetFPS || gSpeedSyncEnabled != gConfiguredSpeedSync))
    {
        ConfigureFramePeriod(target_fps, gSpeedSyncEnabled);
    }

    if (gSpeedSyncEnabled && !gAuxSyncFn && gTicksPerFrame > 0.0L)
    {
        if (gNextFrameDeadline == 0)
        {
            // Do not delay the first visible frame after reset or a target change.
            gNextFrameDeadline = now;
        }
        else
        {
            const u64 period = NextFramePeriodTicks();
            if (UINT64_MAX - gNextFrameDeadline < period)
                gNextFrameDeadline = now;
            else
                gNextFrameDeadline += period;

            // If emulation/rendering already missed the deadline, re-anchor at
            // now rather than issuing a burst of catch-up frames.
            if (now >= gNextFrameDeadline)
                gNextFrameDeadline = now;
            else
                now = WaitUntilDeadline(gNextFrameDeadline);
        }
    }
    else
    {
        gNextFrameDeadline = 0;
    }

#ifdef DAEDALUS_CTR
    if (gLastVITime != 0 && gTicksPerFrame > 0.0L)
        CTRPerfLearning::RecordFrame(now - gLastVITime, (u64)gTicksPerFrame);
#endif
    gLastOrigin = current_origin;
    gLastVITime = now;
    gVblsSinceFlip = 0;
}

f32 FramerateLimiter_GetSync()
{
    if (gCurrentAverageTicksPerVbl == 0 || gTicksBetweenVbls == 0)
        return 0.0f;
    return (f32)((long double)gTicksBetweenVbls /
        (long double)gCurrentAverageTicksPerVbl);
}

// Keep the original emulated VI timing stable. A low rendering rate is not a
// valid reason to change N64 cycles-per-frame (which would speed up gameplay).
f32 FramerateLimiter_GetPerformanceScale()
{
    return 1.0f;
}

u32 FramerateLimiter_GetTargetClockRateHz()
{
    return (gMaxFPS > 120.0f) ? 20000000u : 30000000u;
}

u32 FramerateLimiter_GetHostClockRateHz()
{
    extern bool isN3DS;
    return isN3DS ? 804000000u : 268000000u;
}

u32 FramerateLimiter_GetTvFrequencyHz()
{
    return (u32)(GetConfiguredTargetFPS() + 0.5f);
}
