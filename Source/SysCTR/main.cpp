#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>

#include <3ds.h>
#include <GL/picaGL.h>

#include "BuildOptions.h"
#include "Config/ConfigOptions.h"
#include "Core/Cheats.h"
#include "Core/CPU.h"

#include "Core/Memory.h"
#include "Core/PIF.h"
#include "Core/RomSettings.h"
#include "Core/Save.h"
#include "Debug/DBGConsole.h"
#include "Debug/DebugLog.h"
#include "Graphics/GraphicsContext.h"
#include "HLEGraphics/TextureCache.h"
#include "Input/InputManager.h"
#include "SysCTR/Input/CTRMultiplayer.h"
#include "Interface/RomDB.h"
#include "System/Paths.h"
#include "System/System.h"
#include "Test/BatchTest.h"

#include "UI/UserInterface.h"
#include "UI/RomSelector.h"

#include "SysCTR/Utility/CTRStorage.h"
#include "Utility/IO.h"
#include "Utility/Preferences.h"
#include "Utility/Profiler.h"
#include "Utility/Thread.h"
#include "Utility/Translate.h"
#include "Utility/Timer.h"
#include "Utility/ROMFile.h"
#include "Utility/MemoryCTR.h"

bool isN3DS = false;
bool shouldQuit = false;

EAudioPluginMode enable_audio = APM_ENABLED_ASYNC;

#ifdef DAEDALUS_LOG
void log2file(const char *format, ...) {
	__gnuc_va_list arg;
	int done;
	va_start(arg, format);
	char msg[512];
	done = vsprintf(msg, format, arg);
	va_end(arg);
	sprintf(msg, "%s\n", msg);
	FILE *log = fopen("sdmc:/DaedalusX64.log", "a+");
	if (log != NULL) {
		fwrite(msg, 1, strlen(msg), log);
		fclose(log);
	}
}
#endif

static void CheckDSPFirmware()
{
	FILE *sd_firmware = fopen("sdmc:/3ds/dspfirm.cdc", "rb");
	if(sd_firmware != NULL)
	{
		fclose(sd_firmware);
		return;
	}

	gfxInitDefault();
	consoleInit(GFX_BOTTOM, NULL);
	FILE *firmware = fopen("romfs:/dspfirm.cdc", "rb");
	fclose(firmware);
	printf("\x1b[10;10HFetching DSP component...\x1b[12;10H");

	Handle rsrc = envGetHandle("hb:ndsp");
	if (rsrc)
	{
		Result rc;
		u32 len;
		void* bin;
		extern u32 fake_heap_end;
		const char* filename = "sdmc:/3ds/dspfirm.cdc";

		u32 mapAddr = (fake_heap_end+0xFFF) &~ 0xFFF;
		rc = svcMapMemoryBlock(rsrc, mapAddr, (MemPerm)0x3, (MemPerm)0x3);
		if (R_SUCCEEDED(rc))
		{
			len = *(u32*)(mapAddr + 0x104);
			bin = malloc(len);
			if (bin)
			{
				memcpy(bin, (void*)mapAddr, len);
			}
			svcUnmapMemoryBlock(rsrc, mapAddr);

			if (bin)
			{
				IO::Directory::EnsureExists("sdmc:/3ds");
				FILE* file = fopen(filename, "wb");
				if (file)
				{
					fwrite(bin, 1, len, file);
					fclose(file);
					printf("\x1b[32;1mDone\x1b[0m: DSP firmware dumped successfully!\n");
					free(bin);
					gfxExit();
					return;
				}
				free(bin);
			}
		}
	}

	printf("\x1b[31;1mFailed\x1b[0m: Need to run using *hax 2.0+ or supply dspfirm.cdc\n");
	printf("\x1b[28;15HPress START to exit.");

	while(aptMainLoop())
	{
		hidScanInput();

		if(hidKeysDown() == KEY_START)
		{
			exit(1);
		} else {
			return;
		}
	}
}

static u64 ShaderExtId()
{
    u64 programId = 0;

    APT_GetProgramID(&programId);

    return programId & 0xFFFFFFFFULL;
}

static void ApplyShaderCache(const u8 *cache, size_t got)
{
    if (got < 8)
        return;

    u32 vs = *(const u32 *)&cache[0];
    u32 cs = *(const u32 *)&cache[4];

    if (vs + cs + 8 > got || vs == 0 || cs == 0)
        return;

    pglSetShaderCache(
        cache + 8,
        vs,
        cache + 8 + vs,
        cs
    );
}

static void LoadShaderCache()
{
    u64 extId = ShaderExtId();

    static u8 cache[0x40000];
    size_t got = 0;

    if (CTRStorage::ExtDataRead(
            extId,
            "shader_cache.bin",
            cache,
            sizeof(cache),
            &got))
    {
        ApplyShaderCache(cache, got);
        return;
    }

    if (CTRStorage::ImportFile(
            "sdmc:/3ds/DaedalusX64",
            "shader_cache.bin",
            cache,
            sizeof(cache),
            &got))
    {
        ApplyShaderCache(cache, got);
    }
}

static void SaveShaderCache()
{
    u64 extId = ShaderExtId();

    const void *v;
    const void *c;

    size_t vs;
    size_t cs;

    pglGetShaderCache(&v, &vs, &c, &cs);

    if (vs + cs + 8 > 0x40000)
        return;

    static u8 cache[0x40000];

    *(u32 *)&cache[0] = (u32)vs;
    *(u32 *)&cache[4] = (u32)cs;

    memcpy(cache + 8, v, vs);
    memcpy(cache + 8 + vs, c, cs);

    CTRStorage::ExtDataWrite(
        extId,
        "shader_cache.bin",
        cache,
        vs + cs + 8
    );

    IO::Directory::EnsureExists("sdmc:/3ds/DaedalusX64");

    CTRStorage::ExportFile(
        "sdmc:/3ds/DaedalusX64",
        "shader_cache.bin",
        cache,
        vs + cs + 8
    );
}

static void Initialize()
{
	romfsInit();
	CheckDSPFirmware();
	_InitializeSvcHack();
	
	APT_CheckNew3DS(&isN3DS);
	osSetSpeedupEnable(true);

	gfxInit(GSP_BGR8_OES, GSP_BGR8_OES, true);
	gfxSet3D(true);
	//pglSetStereo(true, 0.020f);
	LoadShaderCache();


	pglInit();
	/* Enable real dual-eye rendering; slider state scales stereo separation. */
	pglSetStereo(true, 0.025f);

	strcpy(gDaedalusExePath, DAEDALUS_CTR_PATH(""));
	strcpy(g_DaedalusConfig.mSaveDir, DAEDALUS_CTR_PATH("SaveGames/"));

	IO::Directory::EnsureExists( DAEDALUS_CTR_PATH("SaveStates/") );
	UI::Initialize();

	System_Init();
}

void HandleEndOfFrame()
{
	shouldQuit = !aptMainLoop();
	if (shouldQuit)
	{
		CPU_Halt("Exiting");
	}
}

extern u32 __ctru_heap_size;

int main(int argc, char* argv[])
{
	Initialize();
	
	while(shouldQuit == false)
	{
		std::string rom = UI::DrawRomSelector();
                std::string full_rom_path = "romfs:/Roms/" + rom;
		System_Open(full_rom_path.c_str());
		CPU_Run();
		CTRMultiplayer::Stop();
		System_Close();
	}
	
	CTRMultiplayer::Stop();
	SaveShaderCache();
	System_Finalize();
	pglExit();

	return 0;
}
