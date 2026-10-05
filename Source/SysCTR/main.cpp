#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
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
#include "SysCTR/DownloadPlayHost.h"
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
	va_list arg;
	char msg[512];
	va_start(arg, format);
	const int written = vsnprintf(msg, sizeof(msg), format, arg);
	va_end(arg);

	// vsnprintf returns the length that would have been written, so clamp it
	// before appending a newline if the formatted message was truncated.
	if (written < 0)
		return;

	size_t length = static_cast<size_t>(written);
	if (length >= sizeof(msg))
		length = sizeof(msg) - 1;
	if (length == 0 || msg[length - 1] != '\n') {
		if (length >= sizeof(msg) - 1)
			length = sizeof(msg) - 2;
		msg[length++] = '\n';
	}
	msg[length] = '\0';

	FILE *log = fopen("sdmc:/DaedalusX64.log", "a+");
	if (log != NULL) {
		fwrite(msg, 1, length, log);
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

static void LoadShaderCache()
{
    // Diagnostic: disable all shader-cache loading (OS ExtData and SD-card
    // mirror) to rule out a stale or invalid cache as the startup failure.
    // SaveData archive handling is unchanged.
}

static void SaveShaderCache()
{
    const void *v;
    const void *c;

    size_t vs;
    size_t cs;

    pglGetShaderCache(&v, &vs, &c, &cs);

    if (!v || !c || vs == 0 || cs == 0 || vs > 0x40000 - 8 || cs > 0x40000 - 8 - vs)
        return;

    static u8 cache[0x40000];

    u32 vertexSize = (u32)vs;
    u32 clearSize = (u32)cs;
    memcpy(cache, &vertexSize, sizeof(vertexSize));
    memcpy(cache + sizeof(vertexSize), &clearSize, sizeof(clearSize));

    memcpy(cache + 8, v, vs);
    memcpy(cache + 8 + vs, c, cs);

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
	// The RSF requests 804 MHz + L2 cache on New 3DS. Enable the runtime
	// speedup only on hardware that supports it; Old 3DS runs at its platform
	// maximum (its CPU clock cannot be raised by an application).
	osSetSpeedupEnable(isN3DS);

	gfxInit(GSP_BGR8_OES, GSP_BGR8_OES, true);
	gfxSet3D(true);
	//pglSetStereo(true, 0.020f);
	LoadShaderCache();


	pglInit();
	/* Enable real dual-eye rendering; slider state scales stereo separation. */
	pglSetStereo(true, 0.050f);

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
	
#ifdef DAEDALUS_DOWNLOADPLAY
	// The temporary DLP child is a single-game build. On the receiving unit,
	// launch straight into the ROM carried in this child title's RomFS rather
	// than presenting the host's regular ROM selector.
	if (aptMainLoop())
	{
		const std::string full_rom_path = std::string("romfs:/Roms/") +
			DAEDALUS_DOWNLOADPLAY_ROM_FILENAME;
		if (System_Open(full_rom_path.c_str()))
		{
			CPU_Run();
			CTRMultiplayer::Stop();
			System_Close();
			// Do not return to the selector in a Download Play child. When the
			// demo session ends, cleanly return to the system title launcher.
			SaveShaderCache();
		}
	}
	shouldQuit = true;
#else
	while(shouldQuit == false)
	{
		std::string rom = UI::DrawRomSelector();
		std::string full_rom_path = "romfs:/Roms/" + rom;
		System_Open(full_rom_path.c_str());
		CPU_Run();
		CTRMultiplayer::Stop();
		System_Close();
		// Persist after the ROM teardown so a title switch or soft exit cannot
		// discard newly compiled shaders.
		SaveShaderCache();
	}
#endif
	
	CTRDownloadPlayHost::Stop();
	CTRMultiplayer::Stop();
	SaveShaderCache();
	System_Finalize();
	pglExit();

	return 0;
}
