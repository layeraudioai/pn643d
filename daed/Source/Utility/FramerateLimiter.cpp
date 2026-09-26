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

static u32				gTicksBetweenVbls = 0;			// How many ticks we want to delay between vertical blanks
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
	1500,		// OS_TV_PAL,
	2600,		// OS_TV_NTSC,
	1500		// OS_TV_MPAL
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

		gTicksBetweenVbls = (u32)(frequency / (u64)gTvFrequencies[ tv_type ]);
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
	static u32 s[4];
	static u32 ptr = 0;

	s[ptr++] = elapsed_ticks;
	ptr &= 0x3;

	//Average 4 frames
	return (s[0] + s[1] + s[2] + s[3] + 2) >> 2;
}

void FramerateLimiter_Limit()
{
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

	gCurrentAverageTicksPerVbl = FramerateLimiter_UpdateAverageTicksPerVbl( elapsed_ticks / gVblsSinceFlip );

	if( gSpeedSyncEnabled && !gAuxSyncFn )
	{
		u32 required_ticks = gTicksBetweenVbls * gVblsSinceFlip;

		if( gSpeedSyncEnabled == 2 ) required_ticks = required_ticks << 1;	// Slow down to 1/2 speed //Corn

		// FIXME the constant here will need to be adjusted for different platforms.
		s32	delay_ticks = required_ticks - elapsed_ticks - 50;	//Remove ~50 ticks for additional processing

		if( delay_ticks > 0 )
		{
			//printf( "Delay ticks: %d\n", delay_ticks );
			ThreadSleepTicks( delay_ticks & 0xFFFF );
			NTiming::GetPreciseTime(&now);
		}
	}

	gLastOrigin = current_origin;
	gLastVITime = now;
	gVblsSinceFlip = 0;

	// AI / Heuristic Learning Control Loop for Dynamic Clock & Ratio Scaling
	// Target optimal framerate always (>120 FPS or high throughput ratio)
	f32 sync = FramerateLimiter_GetSync();
	if (sync > 0.0f)
	{
		f32 target_ratio = sync;
		if (target_ratio < 0.25f) target_ratio = 0.25f;
		if (target_ratio > 8.0f) target_ratio = 8.0f;
		
		// Exponential moving average for learning convergence
		sPerformanceScale = sPerformanceScale * 0.85f + target_ratio * 0.15f;
		if (sPerformanceScale < 0.25f) sPerformanceScale = 0.25f;
		if (sPerformanceScale > 8.0f) sPerformanceScale = 8.0f;
	}
}

f32	FramerateLimiter_GetSync()
{
	if( gCurrentAverageTicksPerVbl == 0 )
	{
		return 0.0f;
	}
	return f32( gTicksBetweenVbls ) / f32( gCurrentAverageTicksPerVbl );
}

f32 FramerateLimiter_GetPerformanceScale()
{
	return sPerformanceScale;
}

u32 FramerateLimiter_GetTargetClockRateHz()
{
	u32 base_clock = g_ROM.rh.ClockRate != 0 ? g_ROM.rh.ClockRate : 93750000;
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
	return gTvFrequencies[ tv_type ];
}
