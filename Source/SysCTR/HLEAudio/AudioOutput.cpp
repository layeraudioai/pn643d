/*
Copyright (C) 2003 Azimer
Copyright (C) 2001,2006 StrmnNrmn

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

#include "AudioOutput.h"

#include <stdio.h>
#include <new>

#include <3ds.h>

#include "SysCTR/HLEAudio/AudioOutput.h"

#include "Config/ConfigOptions.h"
#include "Debug/DBGConsole.h"
#include "HLEAudio/AudioBuffer.h"
#include "Utility/FramerateLimiter.h"
#include "Utility/Thread.h"

static const u32	DESIRED_OUTPUT_FREQUENCY = 44100;
u32	gSoundSync = 44100;
// The producer ring and NDSP submission blocks are user-configurable within
// bounded values. The DSP queue is capped at eight blocks to keep RAM and
// latency predictable.
static const u32	CTR_MAX_WAVE_BUFS = 72;
static const u32	CTR_MIN_WAVE_BUFS = 2;
static const u32	CTR_DEFAULT_STRETCH_SIZE = 512;

static ndspWaveBuf waveBuf[CTR_MAX_WAVE_BUFS];
static u32 waveBufCount = 0;
static u32 waveBufSamples = CTR_DEFAULT_STRETCH_SIZE;

bool audioOpen = false;

static AudioOutput * ac;

static void audioCallback(void *arg)
{
	(void)arg;

	// A callback can be delayed long enough for more than one block to finish.
	// Refill every completed block rather than assuming strict callback/index
	// alternation, otherwise the DSP queue can run dry and produce crackling.
	if (!ac)
		return;
	for (u32 i = 0; i < waveBufCount; ++i)
	{
		if (waveBuf[i].status != NDSP_WBUF_DONE)
			continue;

		ac->FillBuffer(reinterpret_cast<Sample *>(waveBuf[i].data_pcm16), waveBufSamples);
		DSP_FlushDataCache(waveBuf[i].data_pcm16, waveBufSamples * sizeof(Sample));
		ndspChnWaveBufAdd(0, &waveBuf[i]);
	}
}

static u32 ClampCacheSize(u32 value)
{
	if (value < 4096) return 4096;
	if (value > 32768) return 32768;
	return value;
}

static u32 ClampStretchSize(u32 value)
{
	if (value <= 128) return 128;
	if (value <= 256) return 256;
	if (value <= 512) return 512;
	return 1024;
}

static u32 ClampLatency(u32 value)
{
	if (value < 50) return 50;
	if (value > 200) return 200;
	return value;
}

static u32 ClampVolume(u32 value)
{
	return value > 100 ? 100 : value;
}

static bool AudioInit(u32 stretchSize, u32 maxLatencyMs, u32 volume)
{
	if (ndspInit() != 0)
		return false;

	waveBufSamples = ClampStretchSize(stretchSize);
	const u32 targetSamples = (DESIRED_OUTPUT_FREQUENCY * ClampLatency(maxLatencyMs)) / 1000;
	waveBufCount = targetSamples / waveBufSamples;
	if (waveBufCount < CTR_MIN_WAVE_BUFS) waveBufCount = CTR_MIN_WAVE_BUFS;
	if (waveBufCount > CTR_MAX_WAVE_BUFS) waveBufCount = CTR_MAX_WAVE_BUFS;

	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
	ndspChnSetRate(0, (float)DESIRED_OUTPUT_FREQUENCY);
	ndspSetMasterVol((float)ClampVolume(volume) / 100.0f);

	for (u32 i = 0; i < waveBufCount; ++i)
	{
		waveBuf[i].data_vaddr = linearAlloc(waveBufSamples * sizeof(Sample));
		if (waveBuf[i].data_vaddr == nullptr)
		{
			for (u32 j = 0; j < i; ++j)
			{
				linearFree((void *)waveBuf[j].data_vaddr);
				waveBuf[j].data_vaddr = nullptr;
			}
			waveBufCount = 0;
			ndspExit();
			return false;
		}
		waveBuf[i].nsamples = waveBufSamples;
		waveBuf[i].status = 0;
		memset(waveBuf[i].data_pcm16, 0, waveBufSamples * sizeof(Sample));
	}

	ndspSetCallback(&audioCallback, nullptr);
	for (u32 i = 0; i < waveBufCount; ++i)
		ndspChnWaveBufAdd(0, &waveBuf[i]);

	audioOpen = true;
	return true;
}

static void AudioExit()
{
	if (!audioOpen)
		return;

	// Stop stream
	ndspChnWaveBufClear(0);
	ndspExit();

	for (u32 i = 0; i < waveBufCount; ++i)
	{
		linearFree((void *)waveBuf[i].data_vaddr);
		waveBuf[i].data_vaddr = nullptr;
	}
	waveBufCount = 0;

	audioOpen = false;
}

AudioOutput::AudioOutput()
:	mAudioPlaying(false)
,	mExitAudioThread(false)
,	mFrequency(44100)
,	mAudioBuffer(nullptr)
,	mActiveCacheSize(0)
,	mActiveStretchSize(0)
,	mActiveMaxLatencyMs(0)
,	mActiveVolume(0)
{
}

AudioOutput::~AudioOutput()
{
	StopAudio();
	delete mAudioBuffer;
	mAudioBuffer = nullptr;
}

void AudioOutput::SetFrequency( u32 frequency )
{
	mFrequency = frequency;
}

void AudioOutput::AddBuffer( u8 *start, u32 length )
{
	if (length == 0)
		return;

	const u32 cacheSize = ClampCacheSize(gAudioCacheSize);
	const u32 stretchSize = ClampStretchSize(gAudioStretchSize);
	const u32 maxLatencyMs = ClampLatency(gAudioMaxLatencyMs);
	const u32 volume = ClampVolume(gAudioVolume);
	if (mAudioPlaying && (cacheSize != mActiveCacheSize ||
		stretchSize != mActiveStretchSize || maxLatencyMs != mActiveMaxLatencyMs ||
		volume != mActiveVolume))
	{
		// Preferences may be changed while emulating. Stop NDSP before replacing
		// its ring/block resources; the next samples immediately restart it.
		StopAudio();
		delete mAudioBuffer;
		mAudioBuffer = nullptr;
	}
	if (!mAudioPlaying)
		StartAudio();
	if (!mAudioPlaying || !mAudioBuffer)
		return;

	u32 num_samples = length / sizeof( Sample );
	if (mFrequency == 0 || num_samples < 2)
		return;

	u32 output_freq = DESIRED_OUTPUT_FREQUENCY;
	if (gAudioRateMatch)
	{
		output_freq = gSoundSync;
		if (output_freq < DESIRED_OUTPUT_FREQUENCY)
			output_freq = DESIRED_OUTPUT_FREQUENCY;
		if (output_freq > DESIRED_OUTPUT_FREQUENCY * 2)
			output_freq = DESIRED_OUTPUT_FREQUENCY * 2;
	}
	u32 input_freq = mFrequency;

	if (audioOpen)
	{
		ndspChnSetRate(0, (float)DESIRED_OUTPUT_FREQUENCY);
	}

	switch( gAudioPluginEnabled )
	{
	case APM_DISABLED:
		break;

	case APM_ENABLED_ASYNC:
		mAudioBuffer->AddSamples( reinterpret_cast< const Sample * >( start ), num_samples, mFrequency, output_freq );
		break;
	case APM_ENABLED_SYNC:
		mAudioBuffer->AddSamples( reinterpret_cast< const Sample * >( start ), num_samples, input_freq, output_freq );
		break;
	}
}

void AudioOutput::StartAudio()
{
	if (mAudioPlaying)
		return;

	mActiveCacheSize = ClampCacheSize(gAudioCacheSize);
	mActiveStretchSize = ClampStretchSize(gAudioStretchSize);
	mActiveMaxLatencyMs = ClampLatency(gAudioMaxLatencyMs);
	mActiveVolume = ClampVolume(gAudioVolume);
	if (!mAudioBuffer)
		mAudioBuffer = new CAudioBuffer(mActiveCacheSize);

	ac = this;
	mAudioPlaying = AudioInit(mActiveStretchSize, mActiveMaxLatencyMs, mActiveVolume);
	if (!mAudioPlaying && ac == this)
		ac = nullptr;
}

void AudioOutput::StopAudio()
{
	if (!mAudioPlaying)
		return;

	mAudioPlaying = false;
	AudioExit();
	if (ac == this)
		ac = nullptr;
	delete mAudioBuffer;
	mAudioBuffer = nullptr;
}

void AudioOutput::FillBuffer(Sample *buffer, u32 numSamples)
{
	if (mAudioBuffer)
		mAudioBuffer->Drain(buffer, numSamples);
	else
		memset(buffer, 0, numSamples * sizeof(Sample));
}
