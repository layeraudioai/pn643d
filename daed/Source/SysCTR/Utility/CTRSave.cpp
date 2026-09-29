#include "Core/ROM.h"
#include "Core/Memory.h"
#include "Core/Save.h"
#include "Utility/CTRStorage.h"
#include <stdio.h>
#include <string.h>
static bool gSaveDirty=false,gMempackDirty=false; static u32 gSaveSize=0; static char gSaveName[32],gMempackName[32];
static u32 SaveKey(){const char*s=g_ROM.mFileName.c_str();u32 h=2166136261u;for(;*s;s++){h^=(u8)*s;h*=16777619u;}return h;}
static void BuildNames(){u32 h=SaveKey();snprintf(gSaveName,sizeof(gSaveName),"S%08X.sav",h);snprintf(gMempackName,sizeof(gMempackName),"M%08X.mpk",h);}
bool Save_Reset(){BuildNames();switch(g_ROM.settings.SaveType){case SAVE_TYPE_EEP4K:gSaveSize=4096;break;case SAVE_TYPE_EEP16K:gSaveSize=16384;break;case SAVE_TYPE_SRAM:gSaveSize=32768;break;case SAVE_TYPE_FLASH:gSaveSize=131072;break;default:gSaveSize=0;break;}gSaveDirty=false;if(gSaveSize){u8 raw[131072];memset(raw,0,sizeof(raw));if(CTRStorage::SaveDataRead(gSaveName,raw,gSaveSize)){u8*dst=(u8*)g_pMemoryBuffers[MEM_SAVE];for(u32 i=0;i<gSaveSize;i++)dst[i]=raw[i^U8_TWIDDLE];}}memset(g_pMemoryBuffers[MEM_MEMPACK],0,MemoryRegionSizes[MEM_MEMPACK]);if(!CTRStorage::SaveDataRead(gMempackName,g_pMemoryBuffers[MEM_MEMPACK],MemoryRegionSizes[MEM_MEMPACK]))InitMempackContent();gMempackDirty=false;return true;}
void Save_Fini(){Save_Flush(true);} void Save_MarkSaveDirty(){gSaveDirty=true;} void Save_MarkMempackDirty(){gMempackDirty=true;}
void Save_Flush(bool force){if((gSaveDirty||force)&&g_ROM.settings.SaveType!=SAVE_TYPE_UNKNOWN&&gSaveSize){u8 raw[131072];u8*src=(u8*)g_pMemoryBuffers[MEM_SAVE];for(u32 i=0;i<gSaveSize;i++)raw[i^U8_TWIDDLE]=src[i];CTRStorage::SaveDataWrite(gSaveName,raw,gSaveSize);gSaveDirty=false;}if(gMempackDirty||force){CTRStorage::SaveDataWrite(gMempackName,g_pMemoryBuffers[MEM_MEMPACK],MemoryRegionSizes[MEM_MEMPACK]);gMempackDirty=false;}}
