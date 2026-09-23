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

#include "Core/Memory.h"
#include "Core/ROM.h"

static u32				gTicksBetweenVbls = 0;			// How many ticks we want to delay between vertical blanks
static u32				gTicksPerSecond = 0;			// How many ticks there are per second
static u64				gLastVITime = 0;				// The time of the last vertical blank
static u32				gLastOrigin = 0;				// The origin that we saw on the last vertical blank
static u32				gVblsSinceFlip = 0;				// The number of vertical blanks that have occurred since the last n64 flip
static u32				gCurrentAverageTicksPerVbl = 0;
static f32				gPerformanceScale = 1.0f;
static FramerateSyncFn 	gAuxSyncFn = NULL;
static void *			gAuxSyncArg = NULL;

// Nominal guest video timing by N64 TV type (PAL, NTSC, MPAL).
// These are emulated-region rates, not a host-side frame cap.
static const u32		gTvFrequencies[] =
{
	60,		// OS_TV_PAL
	50,		// OS_TV_NTSC
	60		// OS_TV_MPAL
};

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
	gPerformanceScale = 1.0f;

	//gAuxSyncFn  = NULL;	// Should we reset this? Will audio re-init?
	//gAuxSyncArg = NULL;

	if(NTiming::GetPreciseFrequency(&frequency))
	{
		#ifdef DAEDALUS_ENABLE_ASSERTS
		DAEDALUS_ASSERT(g_ROM.TvType < sizeof(gTvFrequencies) / sizeof(gTvFrequencies[0]), "Unknown TV type: %d", g_ROM.TvType);
		#endif

		const u32 tv_type = g_ROM.TvType < sizeof(gTvFrequencies) / sizeof(gTvFrequencies[0]) ? g_ROM.TvType : 0;
		gTicksBetweenVbls = (u32)(frequency / (u64)gTvFrequencies[tv_type]);
		gTicksPerSecond = (u32)frequency;
	}
	else
	{
		gTicksBetweenVbls = 0;
		gTicksPerSecond = 0;
	}
	return true;
}

static u32 FramerateLimiter_UpdateAverageTicksPerVbl( u32 elapsed_ticks )
{
	static u32 s[4] = { 0, 0, 0, 0 };
	static u32 ptr = 0;
	static u32 samples = 0;

	if (samples < 4)
	{
		for (u32 i = 0; i < 4; ++i)
			s[i] = elapsed_ticks;
		samples = 4;
		ptr = 0;
	}
	s[ptr++] = elapsed_ticks;
	ptr &= 0x3;

	// Average the latest four frame intervals without a cold-start bias.
	return (u32)(((u64)s[0] + s[1] + s[2] + s[3] + 2) >> 2);
}

void FramerateLimiter_Limit()
{
	++gVblsSinceFlip;

	// This hook intentionally does not sleep: host rendering is uncapped.
	// Keep audio's optional synchronization callback independent of video pacing.
	if (gAuxSyncFn)
		gAuxSyncFn(gAuxSyncArg);

	const u32 current_origin = Memory_VI_GetRegister(VI_ORIGIN_REG);
	if (current_origin == gLastOrigin)
		return;

	u64 now = 0;
	if (!NTiming::GetPreciseTime(&now))
		return;

	if (gLastVITime != 0 && gVblsSinceFlip != 0)
	{
		const u64 elapsed = now - gLastVITime;
		const u64 per_vbl = elapsed / gVblsSinceFlip;
		gCurrentAverageTicksPerVbl = FramerateLimiter_UpdateAverageTicksPerVbl(
			per_vbl > 0xFFFFFFFFu ? 0xFFFFFFFFu : (u32)per_vbl);

		// Adapt the emulated clock to measured host throughput. Keep it bounded
		// to avoid runaway feedback when the renderer reports a transient spike.
		if (gCurrentAverageTicksPerVbl != 0 && gTicksBetweenVbls != 0)
		{
			const f32 measured_scale = (f32)gTicksBetweenVbls /
				(f32)gCurrentAverageTicksPerVbl;
			const f32 bounded_scale = measured_scale < 0.25f ? 0.25f :
				(measured_scale > 8.0f ? 8.0f : measured_scale);
			gPerformanceScale = (gPerformanceScale * 0.875f) + (bounded_scale * 0.125f);
		}
	}

	gLastOrigin = current_origin;
	gLastVITime = now;
	gVblsSinceFlip = 0;
}

f32	FramerateLimiter_GetSync()
{
	if( gCurrentAverageTicksPerVbl == 0 )
	{
		return 0.0f;
	}
	return f32( gTicksBetweenVbls ) / f32( gCurrentAverageTicksPerVbl );
}

u32 FramerateLimiter_GetTvFrequencyHz()
{
	const u32 tv_type = g_ROM.TvType < sizeof(gTvFrequencies) / sizeof(gTvFrequencies[0]) ? g_ROM.TvType : 0;
	return gTvFrequencies[tv_type];
}

f32 FramerateLimiter_GetPerformanceScale()
{
	return gPerformanceScale;
}

u64 FramerateLimiter_GetTargetClockRateHz()
{
	// The emulated N64 clock is a guest value and remains independent from the
	// 3DS SoC clock. Scale only its runtime budget using measured throughput.
	const u64 nominal_clock = g_ROM.rh.ClockRate != 0 ? g_ROM.rh.ClockRate : 93750000u;
	return (u64)((f64)nominal_clock * (f64)gPerformanceScale);
}

u64 FramerateLimiter_GetHostClockRateHz()
{
#if defined(DAEDALUS_CTR)
	extern bool isN3DS;
	const u64 nominal_host_clock = isN3DS ? 804000000ull : 268000000ull;
	// This reports an effective throughput-adjusted host clock estimate; it
	// does not alter libctru's fixed svcGetSystemTick() clock source.
	return (u64)((f64)nominal_host_clock * (f64)gPerformanceScale);
#else
	return (u64)((f64)gTicksPerSecond * (f64)gPerformanceScale);
#endif
}
