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

<<<<<<< HEAD
static u32				gTicksBetweenVbls = 0;			// User-selected frame-cap interval
static u32				gTicksBetweenBackendVbls = 0;		// Adaptive backend VI interval
static u32				gBackendMaxFPS = 60;
static u32				gBackendCeilingFPS = 120;
static u32				gUserTargetFPS = 60;
static u64				gClockFrequency = 0;
static u32				gTicksPerSecond = 0;			// How many ticks there are per second
static u64				gLastVITime = 0;				// The time of the last vertical blank
static u32				gLastOrigin = 0;				// The origin that we saw on the last vertical blank
static u32				gVblsSinceFlip = 0;				// The number of vertical blanks that have occurred since the last n64 flip
static u32				gCurrentAverageTicksPerVbl = 0;
static f32				sPerformanceScale = 1.0f;
static FramerateSyncFn 	gAuxSyncFn = NULL;
static void *			gAuxSyncArg = NULL;
=======
#include <stdint.h>
>>>>>>> 9b15d14f43462799691a3b08bffae82f9bd0d18d

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

<<<<<<< HEAD
	gLastVITime = 0;
	gLastOrigin = 0;
	gVblsSinceFlip = 0;
	gCurrentAverageTicksPerVbl = 0;
	sPerformanceScale = 1.0f;

	//gAuxSyncFn  = NULL;	// Should we reset this? Will audio re-init?
	//gAuxSyncArg = NULL;

	if(NTiming::GetPreciseFrequency(&frequency))
	{
		u32 tv_type = g_ROM.TvType;
		if (tv_type >= sizeof(gTvFrequencies) / sizeof(u32))
		{
			tv_type = 0;
		}

		#ifdef DAEDALUS_ENABLE_ASSERTS
		DAEDALUS_ASSERT(tv_type < sizeof(gTvFrequencies) / sizeof(u32), "Unknown TV type: %d", g_ROM.TvType);
		#endif

		u32 target_fps = (gMaxFPS > 0.0f) ? (u32)gMaxFPS : gTvFrequencies[ tv_type ];
		if (target_fps == 0) target_fps = 60;
		// Keep the user-selected limit as a hard wall-clock cap. The backend
		// VI rate is separate and may rise to improve games that update more
		// often when they receive additional VI interrupts.
		gClockFrequency = frequency;
		gUserTargetFPS = target_fps;
		gTicksBetweenVbls = (u32)(frequency / (u64)target_fps);
		gBackendMaxFPS = target_fps;
#ifdef DAEDALUS_CTR
		gBackendCeilingFPS = CTRPerfLearning::GetRecommendedBackendCeilingFPS(target_fps);
		if (gBackendCeilingFPS < target_fps) gBackendCeilingFPS = target_fps;
#else
		gBackendCeilingFPS = target_fps < 120u ? target_fps * 2u : 240u;
#endif
		gTicksBetweenBackendVbls = (u32)(frequency / (u64)gBackendMaxFPS);
		gTicksPerSecond = (u32)((frequency * ((f32)target_fps / 60.0f)) * 3);
	}
	else
	{
		gClockFrequency = 0;
		gTicksBetweenVbls = 0;
		gTicksBetweenBackendVbls = 0;
		gBackendMaxFPS = 0;
		gBackendCeilingFPS = 0;
		gUserTargetFPS = 0;
		gTicksPerSecond = 0;
	}
	return true;
}

static u32 FramerateLimiter_UpdateAverageTicksPerVbl( u32 elapsed_ticks )
{
	static u32 s[4];
	static u32 ptr = 0;

	s[ptr++] = elapsed_ticks;
	ptr &= 0x3;

	//Average 4 frames
	return (s[0] + s[1] + s[2] + s[3] + 2) >> 2;
=======
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
>>>>>>> 9b15d14f43462799691a3b08bffae82f9bd0d18d
}

void FramerateLimiter_Limit()
{
<<<<<<< HEAD
#ifdef DAEDALUS_CTR
	CTR_PERF_SCOPE(CTRPerfLearning::PROFILE_FRAME_LIMITER);
#endif
	// MaxFPS can be changed from the in-game menu. Apply it live without
	// resetting the timer (or the adaptive backend) on every menu draw.
	u32 tv_type = g_ROM.TvType;
	if (tv_type >= sizeof(gTvFrequencies) / sizeof(u32)) tv_type = 0;
	u32 target_fps = (gMaxFPS > 0.0f) ? (u32)gMaxFPS : gTvFrequencies[tv_type];
	if (target_fps == 0) target_fps = 60;
	if (gClockFrequency != 0 && target_fps != gUserTargetFPS)
	{
		gUserTargetFPS = target_fps;
		gTicksBetweenVbls = (u32)(gClockFrequency / target_fps);
		gBackendMaxFPS = target_fps;
#ifdef DAEDALUS_CTR
		gBackendCeilingFPS = CTRPerfLearning::GetRecommendedBackendCeilingFPS(target_fps);
		if (gBackendCeilingFPS < target_fps) gBackendCeilingFPS = target_fps;
#else
		gBackendCeilingFPS = target_fps > 120u ? 240u : target_fps * 2u;
#endif
		gTicksBetweenBackendVbls = (u32)(gClockFrequency / gBackendMaxFPS);
	}

	gVblsSinceFlip++;
=======
    ++gVblsSinceFlip;
>>>>>>> 9b15d14f43462799691a3b08bffae82f9bd0d18d

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

<<<<<<< HEAD
	u32 elapsed_ticks = (u32)(now - gLastVITime);
	const u32 completed_vbls = gVblsSinceFlip == 0 ? 1u : gVblsSinceFlip;
	const u32 work_per_vbl = elapsed_ticks / completed_vbls;
	gCurrentAverageTicksPerVbl = FramerateLimiter_UpdateAverageTicksPerVbl(work_per_vbl);
#ifdef DAEDALUS_CTR
	CTRPerfLearning::RecordFrame(work_per_vbl, gTicksBetweenVbls);
	gBackendCeilingFPS = CTRPerfLearning::GetRecommendedBackendCeilingFPS(gUserTargetFPS);
	if (gBackendCeilingFPS < gUserTargetFPS) gBackendCeilingFPS = gUserTargetFPS;
#endif

	if( !gAuxSyncFn && gTicksBetweenVbls != 0 )
	{
		// The backend may generate more VI interrupts than the user's requested
		// output rate. Pace each flip at the slower of the backend cadence and
		// the explicit user cap, so menus can never run past MaxFPS.
		u64 required_ticks = (u64)gTicksBetweenBackendVbls * gVblsSinceFlip;
		if (required_ticks < gTicksBetweenVbls)
			required_ticks = gTicksBetweenVbls;
		if( gSpeedSyncEnabled == 2 ) required_ticks *= 2;	// Slow down to 1/2 speed //Corn

		const u64 processing_ticks = elapsed_ticks;
		if (required_ticks > processing_ticks)
		{
			const u64 delay_ticks = required_ticks - processing_ticks;
			ThreadSleepTicks(delay_ticks > 0xFFFFFFFFu ? 0xFFFFFFFFu : (u32)delay_ticks);
			NTiming::GetPreciseTime(&now);
		}
	}

	gLastOrigin = current_origin;
	gLastVITime = now;
	gVblsSinceFlip = 0;

	// Adapt the internal VI cadence only when the previous frame had headroom.
	// The backend has a bounded ceiling (at most 240 VI/s, or the user target
	// when that is higher); it backs off promptly if emulation cannot keep up.
	if (gClockFrequency != 0 && gBackendMaxFPS != 0)
	{
		const u64 measured_work_per_vbl = work_per_vbl;
		const u64 backend_period = gTicksBetweenBackendVbls;
		if (measured_work_per_vbl * 10 < backend_period * 8 &&
			gBackendMaxFPS < gBackendCeilingFPS)
		{
			gBackendMaxFPS += gBackendCeilingFPS - gBackendMaxFPS < 5u ?
				gBackendCeilingFPS - gBackendMaxFPS : 5u;
		}
		else if (measured_work_per_vbl * 10 > backend_period * 12 &&
			gBackendMaxFPS > (u32)gMaxFPS)
		{
			gBackendMaxFPS = gBackendMaxFPS - (u32)gMaxFPS < 5u ?
				(u32)gMaxFPS : gBackendMaxFPS - 5u;
		}
		gTicksBetweenBackendVbls = (u32)(gClockFrequency / gBackendMaxFPS);
	}
=======
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
>>>>>>> 9b15d14f43462799691a3b08bffae82f9bd0d18d
}

f32 FramerateLimiter_GetSync()
{
<<<<<<< HEAD
	if( gCurrentAverageTicksPerVbl == 0 )
	{
		return 0.0f;
	}
	return f32( gTicksBetweenBackendVbls ) / f32( gCurrentAverageTicksPerVbl );
=======
    if (gCurrentAverageTicksPerVbl == 0 || gTicksBetweenVbls == 0)
        return 0.0f;
    return (f32)((long double)gTicksBetweenVbls /
        (long double)gCurrentAverageTicksPerVbl);
>>>>>>> 9b15d14f43462799691a3b08bffae82f9bd0d18d
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

u32 FramerateLimiter_GetBackendMaxFPS()
{
	return gBackendMaxFPS != 0 ? gBackendMaxFPS : FramerateLimiter_GetTvFrequencyHz();
}
