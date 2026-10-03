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
// Extra capacity absorbs short emulation/DSP scheduling stalls. This is not
// fixed latency: the ring only accumulates audio when production gets ahead.
static const u32	BUFFER_SIZE  = 1024 * 8;

static const u32	CTR_NUM_SAMPLES = 512;
static const u32	CTR_NUM_WAVE_BUFS = 4;

static ndspWaveBuf waveBuf[CTR_NUM_WAVE_BUFS];

bool audioOpen = false;

static AudioOutput * ac;

CAudioBuffer *mAudioBuffer;

static void audioCallback(void *arg)
{
	(void)arg;

	// A callback can be delayed long enough for more than one block to finish.
	// Refill every completed block rather than assuming strict callback/index
	// alternation, otherwise the DSP queue can run dry and produce crackling.
	for (u32 i = 0; i < CTR_NUM_WAVE_BUFS; ++i)
	{
		if (waveBuf[i].status != NDSP_WBUF_DONE)
			continue;

		mAudioBuffer->Drain( reinterpret_cast< Sample * >( waveBuf[i].data_pcm16 ), CTR_NUM_SAMPLES );
		DSP_FlushDataCache(waveBuf[i].data_pcm16, CTR_NUM_SAMPLES << 2);
		ndspChnWaveBufAdd( 0, &waveBuf[i] );
	}
}

static void AudioInit()
{
	if (ndspInit() != 0)
		return;

	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
	
	ndspChnSetRate(0, 44100.0f);

	for (u32 i = 0; i < CTR_NUM_WAVE_BUFS; ++i)
	{
		waveBuf[i].data_vaddr = linearAlloc(CTR_NUM_SAMPLES * sizeof(Sample));
		if (waveBuf[i].data_vaddr == nullptr)
		{
			for (u32 j = 0; j < i; ++j)
				linearFree((void *)waveBuf[j].data_vaddr);
			ndspExit();
			return;
		}
		waveBuf[i].nsamples = CTR_NUM_SAMPLES;
		waveBuf[i].status = 0;
		memset(waveBuf[i].data_pcm16, 0, CTR_NUM_SAMPLES * sizeof(Sample));
	}

	ndspSetCallback(&audioCallback, nullptr);

	for (u32 i = 0; i < CTR_NUM_WAVE_BUFS; ++i)
		ndspChnWaveBufAdd(0, &waveBuf[i]);

	// Everything OK
	audioOpen = true;
}

static void AudioExit()
{
	if (!audioOpen)
		return;

	// Stop stream
	ndspChnWaveBufClear(0);
	ndspExit();

	for (u32 i = 0; i < CTR_NUM_WAVE_BUFS; ++i)
	{
		linearFree((void *)waveBuf[i].data_vaddr);
		waveBuf[i].data_vaddr = nullptr;
	}

	audioOpen = false;
}

AudioOutput::AudioOutput()
:	mAudioPlaying( false )
,	mFrequency( 44100 )
{
	// Allocate audio buffer with malloc_64 to avoid cached/uncached aliasing
	void * mem = malloc( sizeof( CAudioBuffer ) );
	mAudioBuffer = new( mem ) CAudioBuffer( BUFFER_SIZE );
}

AudioOutput::~AudioOutput( )
{
	StopAudio();

	mAudioBuffer->~CAudioBuffer();
	free( mAudioBuffer );
}

void AudioOutput::SetFrequency( u32 frequency )
{
	mFrequency = frequency;
}

void AudioOutput::AddBuffer( u8 *start, u32 length )
{
	if (length == 0)
		return;

	if (!mAudioPlaying)
		StartAudio();

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

	mAudioPlaying = true;

	ac = this;

	AudioInit();
}

void AudioOutput::StopAudio()
{
	if (!mAudioPlaying)
		return;

	mAudioPlaying = false;

	AudioExit();
}
