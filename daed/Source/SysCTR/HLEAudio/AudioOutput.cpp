/*
Copyright (C) 2003 Azimer
Copyright (C) 2001,2006 StrmnNrmn
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
static const u32 DESIRED_OUTPUT_FREQUENCY=44100; u32 gSoundSync=44100; static const u32 BUFFER_SIZE=4096; static const u32 CTR_NUM_SAMPLES=512;
static ndspWaveBuf waveBuf[2]; static unsigned int waveBuf_id; bool audioOpen=false; static AudioOutput*ac; CAudioBuffer*mAudioBuffer;
static const float MIN_STRETCH=0.985f,MAX_STRETCH=1.015f;
static void UpdateAudioStretch(){if(!audioOpen)return;float target=BUFFER_SIZE*0.50f;float buffered=(float)mAudioBuffer->GetNumBufferedSamples();float error=(buffered-target)/target;if(error>1)error=1;if(error<-1)error=-1;float ratio=1.0f+error*0.015f;if(ratio<MIN_STRETCH)ratio=MIN_STRETCH;if(ratio>MAX_STRETCH)ratio=MAX_STRETCH;ndspChnSetRate(0,(float)DESIRED_OUTPUT_FREQUENCY*ratio);}
static void audioCallback(void*arg){(void)arg;if(waveBuf[waveBuf_id].status==NDSP_WBUF_DONE){mAudioBuffer->Drain(reinterpret_cast<Sample*>(waveBuf[waveBuf_id].data_pcm16),CTR_NUM_SAMPLES);DSP_FlushDataCache(waveBuf[waveBuf_id].data_pcm16,CTR_NUM_SAMPLES<<2);ndspChnWaveBufAdd(0,&waveBuf[waveBuf_id]);waveBuf_id=!waveBuf_id;UpdateAudioStretch();}}
static void AudioInit(){if(ndspInit()!=0)return;ndspSetOutputMode(NDSP_OUTPUT_STEREO);ndspChnSetFormat(0,NDSP_FORMAT_STEREO_PCM16);ndspChnSetRate(0,44100.0f);for(int i=0;i<2;i++){waveBuf[i].data_vaddr=linearAlloc(CTR_NUM_SAMPLES*4);waveBuf[i].nsamples=CTR_NUM_SAMPLES;waveBuf[i].status=0;memset(waveBuf[i].data_pcm16,0,CTR_NUM_SAMPLES*4);}waveBuf_id=0;ndspSetCallback(&audioCallback,nullptr);ndspChnWaveBufAdd(0,&waveBuf[0]);ndspChnWaveBufAdd(0,&waveBuf[1]);audioOpen=true;}
static void AudioExit(){ndspChnWaveBufClear(0);ndspExit();linearFree((void*)waveBuf[0].data_vaddr);linearFree((void*)waveBuf[1].data_vaddr);audioOpen=false;}
AudioOutput::AudioOutput():mAudioPlaying(false),mFrequency(44100){void*mem=malloc(sizeof(CAudioBuffer));mAudioBuffer=new(mem)CAudioBuffer(BUFFER_SIZE);}
AudioOutput::~AudioOutput(){StopAudio();mAudioBuffer->~CAudioBuffer();free(mAudioBuffer);}
void AudioOutput::SetFrequency(u32 frequency){mFrequency=frequency;}
void AudioOutput::AddBuffer(u8*start,u32 length){if(!length)return;if(!mAudioPlaying)StartAudio();u32 num_samples=length/sizeof(Sample);u32 output_freq=DESIRED_OUTPUT_FREQUENCY,input_freq=mFrequency;if(audioOpen)UpdateAudioStretch();switch(gAudioPluginEnabled){case APM_DISABLED:break;case APM_ENABLED_ASYNC:case APM_ENABLED_SYNC:mAudioBuffer->AddSamples(reinterpret_cast<const Sample*>(start),num_samples,input_freq,output_freq);UpdateAudioStretch();break;}}
void AudioOutput::StartAudio(){if(mAudioPlaying)return;mAudioPlaying=true;ac=this;AudioInit();}
void AudioOutput::StopAudio(){if(!mAudioPlaying)return;mAudioPlaying=false;AudioExit();}
