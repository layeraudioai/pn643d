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

static const u32		gTvFrequencies[] =
{
	240,		// OS_TV_PAL,
	240,		// OS_TV_NTSC,
	240		// OS_TV_MPAL
};

extern float gMaxFPS;

void FramerateLimiter_SetAuxillarySyncFunction(FramerateSyncFn fn, void * arg)
{
	gAuxSyncFn  = fn;
	gAuxSyncArg = arg;
}

bool FramerateLimiter_Reset()
{
	u64 frequency;

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
}

void FramerateLimiter_Limit()
{
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

	// Only do framerate limiting on frames that correspond to a flip
	u32 current_origin = Memory_VI_GetRegister(VI_ORIGIN_REG);

	if (gAuxSyncFn)
	{
		gAuxSyncFn(gAuxSyncArg);
	}

	if( current_origin == gLastOrigin )
		return;

	u64	now;
	NTiming::GetPreciseTime(&now);

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
}

f32	FramerateLimiter_GetSync()
{
	if( gCurrentAverageTicksPerVbl == 0 )
	{
		return 0.0f;
	}
	return f32( gTicksBetweenBackendVbls ) / f32( gCurrentAverageTicksPerVbl );
}

f32 FramerateLimiter_GetPerformanceScale()
{
	return sPerformanceScale;
}

u32 FramerateLimiter_GetTargetClockRateHz()
{
	u32 base_clock = (gMaxFPS > 120.0f) ? 20000000u : 30000000u;
	return (u32)((f32)base_clock * sPerformanceScale);
}

u32 FramerateLimiter_GetHostClockRateHz()
{
	extern bool isN3DS;
	u32 base_host_clock = isN3DS ? 804000000u : 268000000u;
	return (u32)((f32)base_host_clock * sPerformanceScale);
}

u32 FramerateLimiter_GetTvFrequencyHz()
{
	u32 tv_type = g_ROM.TvType;
	if (tv_type >= sizeof(gTvFrequencies) / sizeof(u32))
	{
		tv_type = 0;
	}
	return (gMaxFPS > 0.0f) ? (u32)gMaxFPS : gTvFrequencies[ tv_type ];
}

u32 FramerateLimiter_GetBackendMaxFPS()
{
	return gBackendMaxFPS != 0 ? gBackendMaxFPS : FramerateLimiter_GetTvFrequencyHz();
}
