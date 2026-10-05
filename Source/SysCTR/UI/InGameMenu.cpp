#include <3ds.h>
#include <GL/picaGL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Defines platform calling-convention and attribute macros used by Core headers.
#include "BuildOptions.h"

#include "UserInterface.h"
#include "InGameMenu.h"

#include "Config/ConfigOptions.h"
#include "Core/CPU.h"
#include "Core/ROM.h"
#include "SysCTR/Input/CTRInput.h"
#include "SysCTR/Input/CTRMultiplayer.h"
#include "SysCTR/DownloadPlayHost.h"
#include "Utility/IO.h"
#include "Utility/Preferences.h"
#include "Utility/FramerateLimiter.h"

extern uint8_t aspectRatio;
extern float gCurrentFramerate;
extern RomInfo g_ROM;

static uint64_t timer;
static uint8_t currentPage = 0;
static uint8_t optionsSubpage = 0;
static bool PromptNumericText(const char *hint, const char *initialValue,
	char *buffer, size_t bufferSize, bool allowDecimal);

// The speedrun clock is deliberately kept outside save states so loading a
// state cannot rewind the run. Persist accumulated time per ROM, keyed by its
// CRC/country ID rather than its display name.
static bool sSpeedrunInitialized = false;
static bool sSpeedrunRunning = false;
static uint64_t sSpeedrunElapsedMs = 0;
static uint64_t sSpeedrunStartedAtMs = 0;
static uint32_t sSpeedrunCRC1 = 0;
static uint32_t sSpeedrunCRC2 = 0;
static uint8_t sSpeedrunCountryID = 0;

static void GetSpeedrunPathForROM(IO::Filename path, uint32_t crc1, uint32_t crc2, uint8_t countryID)
{
	snprintf(path, sizeof(IO::Filename), "%sSaveStates/Speedrun_%08X_%08X_%02X.dat",
		DAEDALUS_CTR_PATH(""), (unsigned int)crc1, (unsigned int)crc2,
		(unsigned int)countryID);
}

static void GetSpeedrunPath(IO::Filename path)
{
	GetSpeedrunPathForROM(path, g_ROM.mRomID.CRC[0], g_ROM.mRomID.CRC[1],
		g_ROM.mRomID.CountryID);
}

static void SaveSpeedrunTime()
{
	IO::Filename path;
	GetSpeedrunPathForROM(path, sSpeedrunCRC1, sSpeedrunCRC2, sSpeedrunCountryID);
	IO::Directory::EnsureExists(DAEDALUS_CTR_PATH("SaveStates"));

	FILE *file = fopen(path, "wb");
	if (file)
	{
		fprintf(file, "DAEDALUS_SPEEDRUN_V1 %llu",
			(unsigned long long)sSpeedrunElapsedMs);
		fclose(file);
	}
}

static void EnsureSpeedrunTimerLoaded()
{
	if (sSpeedrunInitialized &&
		sSpeedrunCRC1 == g_ROM.mRomID.CRC[0] &&
		sSpeedrunCRC2 == g_ROM.mRomID.CRC[1] &&
		sSpeedrunCountryID == g_ROM.mRomID.CountryID)
		return;

	// Commit the previous game's timer before switching the cached ROM identity.
	if (sSpeedrunInitialized && sSpeedrunRunning)
	{
		sSpeedrunElapsedMs += osGetTime() - sSpeedrunStartedAtMs;
		sSpeedrunRunning = false;
		SaveSpeedrunTime();
	}

	sSpeedrunInitialized = true;
	sSpeedrunRunning = false;
	sSpeedrunElapsedMs = 0;
	sSpeedrunCRC1 = g_ROM.mRomID.CRC[0];
	sSpeedrunCRC2 = g_ROM.mRomID.CRC[1];
	sSpeedrunCountryID = g_ROM.mRomID.CountryID;

	IO::Filename path;
	GetSpeedrunPath(path);
	FILE *file = fopen(path, "rb");
	if (file)
	{
		char header[32] = {};
		unsigned long long elapsed = 0;
		if (fscanf(file, "%31s %llu", header, &elapsed) == 2 &&
			strcmp(header, "DAEDALUS_SPEEDRUN_V1") == 0)
			sSpeedrunElapsedMs = (uint64_t)elapsed;
		fclose(file);
	}
}

static uint64_t GetSpeedrunElapsedMs()
{
	return sSpeedrunElapsedMs + (sSpeedrunRunning ? osGetTime() - sSpeedrunStartedAtMs : 0);
}

static void DrawSpeedrunTimer()
{
	EnsureSpeedrunTimerLoaded();

	const uint64_t elapsed = GetSpeedrunElapsedMs();
	const uint64_t centiseconds = elapsed / 10;
	const unsigned long long hours = (unsigned long long)(centiseconds / 360000);
	const unsigned long long minutes = (unsigned long long)((centiseconds / 6000) % 60);
	const unsigned long long seconds = (unsigned long long)((centiseconds / 100) % 60);
	const unsigned long long hundredths = (unsigned long long)(centiseconds % 100);
	char timerString[64];
	char controlString[16];
	snprintf(timerString, sizeof(timerString), "Speedrun: %02llu:%02llu:%02llu.%02llu",
		hours, minutes, seconds, hundredths);
	snprintf(controlString, sizeof(controlString), "%s timer", sSpeedrunRunning ? "Pause" : "Start");
	UI::DrawText(10, 158, timerString);

	if (UI::DrawButton(10, 166, 145, 32, controlString))
	{
		EnsureSpeedrunTimerLoaded();
		if (sSpeedrunRunning)
		{
			sSpeedrunElapsedMs += osGetTime() - sSpeedrunStartedAtMs;
			sSpeedrunRunning = false;
		}
		else
		{
			sSpeedrunStartedAtMs = osGetTime();
			sSpeedrunRunning = true;
		}
		SaveSpeedrunTime();
	}

	if (UI::DrawButton(165, 166, 145, 32, "Reset timer"))
	{
		EnsureSpeedrunTimerLoaded();
		sSpeedrunElapsedMs = 0;
		sSpeedrunRunning = false;
		SaveSpeedrunTime();
	}
}

static void ExecSaveState(int slot)
{
	IO::Filename full_path;
	sprintf(full_path, "%s%s.ss%d", DAEDALUS_CTR_PATH("SaveStates/"), g_ROM.settings.GameName.c_str(), slot);

	CPU_RequestSaveState(full_path);
}

static void LoadSaveState(int slot)
{
	IO::Filename full_path;
	sprintf(full_path, "%s%s.ss%d", DAEDALUS_CTR_PATH("SaveStates/"), g_ROM.settings.GameName.c_str(), slot);

	CPU_RequestLoadState(full_path);
}

static void DrawSaveStatePage()
{
	char buttonString[20];

	UI::DrawHeader("Save state");

	for(int i = 0; i < 3; i++)
	{
		sprintf(buttonString, "Save slot: %i", i);

		if(UI::DrawButton(10, 22 + (42 * i), 300, 36, buttonString))
		{
			ExecSaveState(i);
		}
	}

	DrawSpeedrunTimer();
	if(UI::DrawButton(10, 204, 300, 28, "Back"))
		currentPage = 0;
}

static void DrawLoadStatePage()
{
	char buttonString[20];

	UI::DrawHeader("Load state");

	for(int i = 0; i < 3; i++)
	{
		sprintf(buttonString, "Load slot: %i", i);

		if(UI::DrawButton(10, 22 + (42 * i), 300, 36, buttonString))
		{
			LoadSaveState(i);
		}
	}

	DrawSpeedrunTimer();
	if(UI::DrawButton(10, 204, 300, 28, "Back"))
		currentPage = 0;
}

static void DrawConfirmPage()
{
	UI::DrawHeader("Close ROM: Are you sure?");

	if(UI::DrawToggle(10,  22, 300, 99, "YES", false))
	{
		currentPage = 0;
		 CPU_Halt("Exiting");
	}

	if(UI::DrawButton(10, 131, 300, 99, "NO"))
	{
		currentPage = 0;
	}
	
}

static void DrawOptionsPage()
{
	SRomPreferences preferences;
	CPreferences::Get()->GetRomPreferences( g_ROM.mRomID, &preferences );

	char header[32];
	snprintf(header, sizeof(header), "Options %u/5", (unsigned int)optionsSubpage + 1);
	UI::DrawHeader(header);

	// Audio modes are intentionally cycled independently of speed sync: audio
	// output and video/frame pacing are separate user choices.
	if (optionsSubpage == 0)
	{
		char label[48];
		switch (preferences.AudioEnabled)
		{
			case APM_DISABLED:     snprintf(label, sizeof(label), "Audio: Off"); break;
			case APM_ENABLED_SYNC: snprintf(label, sizeof(label), "Audio: Sync"); break;
			default:               snprintf(label, sizeof(label), "Audio: Async"); break;
		}
		if (UI::DrawButton(10, 22, 145, 32, label))
			preferences.AudioEnabled = static_cast<EAudioPluginMode>((preferences.AudioEnabled + 1) % 3);

		snprintf(label, sizeof(label), "Video sync: %s", preferences.VideoRateMatch ? "Sync" : "Async");
		if (UI::DrawToggle(165, 22, 145, 32, label, preferences.VideoRateMatch))
			preferences.VideoRateMatch = !preferences.VideoRateMatch;

		if (UI::DrawToggle(10, 59, 145, 32, "Audio rate match", preferences.AudioRateMatch))
			preferences.AudioRateMatch = !preferences.AudioRateMatch;

		snprintf(label, sizeof(label), "Frameskip: %s", Preferences_GetFrameskipDescription(preferences.Frameskip));
		if (UI::DrawButton(10, 96, 145, 32, label))
			preferences.Frameskip = static_cast<EFrameskipValue>((preferences.Frameskip + 1) % NUM_FRAMESKIP_VALUES);

		snprintf(label, sizeof(label), "Max FPS: %.0f", preferences.MaxFPS);
		if (UI::DrawButton(165, 96, 145, 32, label))
		{
			char valueText[16];
			snprintf(valueText, sizeof(valueText), "%.0f", preferences.MaxFPS);
			if (PromptNumericText("Maximum FPS (1-500)", valueText, valueText, sizeof(valueText), false))
			{
				char *end = NULL;
				unsigned long value = strtoul(valueText, &end, 10);
				if (end != valueText && *end == '\0' && value >= 1 && value <= 500)
					preferences.MaxFPS = (float)value;
			}
		}

		if (UI::DrawToggle(10, 133, 145, 32, "Aspect ratio", aspectRatio != 0))
			aspectRatio = !aspectRatio;
		if (UI::DrawToggle(165, 133, 145, 32, "Speed sync", preferences.SpeedSyncEnabled != 0))
			preferences.SpeedSyncEnabled = preferences.SpeedSyncEnabled ? 0 : 1;

		snprintf(label, sizeof(label), "Stereo: %.3f", preferences.StereoSeparation);
		if (UI::DrawButton(10, 170, 145, 32, label))
		{
			char valueText[16];
			snprintf(valueText, sizeof(valueText), "%.3f", preferences.StereoSeparation);
			if (PromptNumericText("Stereo separation (0-0.2)", valueText, valueText, sizeof(valueText), true))
			{
				char *end = NULL;
				float value = strtof(valueText, &end);
				if (end != valueText && *end == '\0' && value >= 0.0f && value <= 0.2f)
					preferences.StereoSeparation = value;
			}
		}
		if (UI::DrawToggle(165, 170, 145, 32,
			preferences.StereoPopout ? "3D: Pop-out" : "3D: Depth", preferences.StereoPopout))
			preferences.StereoPopout = !preferences.StereoPopout;
	}
	else if (optionsSubpage == 1)
	{
		char label[48];
		snprintf(label, sizeof(label), "Audio cache: %uKB", (unsigned int)(preferences.AudioCacheSize / 256));
		if (UI::DrawButton(10, 22, 145, 32, label))
		{
			char valueText[16];
			snprintf(valueText, sizeof(valueText), "%u", (unsigned int)(preferences.AudioCacheSize / 256));
			if (PromptNumericText("Audio cache KB (16, 32, 64, 128)", valueText, valueText, sizeof(valueText), false))
			{
				unsigned long value = strtoul(valueText, NULL, 10);
				if (value == 16 || value == 32 || value == 64 || value == 128)
					preferences.AudioCacheSize = (u32)(value * 256);
			}
		}

		snprintf(label, sizeof(label), "Stretch size: %u", (unsigned int)preferences.AudioStretchSize);
		if (UI::DrawButton(165, 22, 145, 32, label))
		{
			char valueText[16];
			snprintf(valueText, sizeof(valueText), "%u", (unsigned int)preferences.AudioStretchSize);
			if (PromptNumericText("DSP stretch samples (128-1024)", valueText, valueText, sizeof(valueText), false))
			{
				unsigned long value = strtoul(valueText, NULL, 10);
				if (value == 128 || value == 256 || value == 512 || value == 1024)
					preferences.AudioStretchSize = (u32)value;
			}
		}

		snprintf(label, sizeof(label), "Max latency: %ums", (unsigned int)preferences.AudioMaxLatencyMs);
		if (UI::DrawButton(10, 59, 145, 32, label))
		{
			char valueText[16];
			snprintf(valueText, sizeof(valueText), "%u", (unsigned int)preferences.AudioMaxLatencyMs);
			if (PromptNumericText("Audio latency ms (50-200)", valueText, valueText, sizeof(valueText), false))
			{
				char *end = NULL;
				unsigned long value = strtoul(valueText, &end, 10);
				if (end != valueText && *end == '\0' && value >= 50 && value <= 200)
					preferences.AudioMaxLatencyMs = (u32)value;
			}
		}

		snprintf(label, sizeof(label), "Volume: %u%%", (unsigned int)preferences.AudioVolume);
		if (UI::DrawButton(165, 59, 145, 32, label))
		{
			char valueText[16];
			snprintf(valueText, sizeof(valueText), "%u", (unsigned int)preferences.AudioVolume);
			if (PromptNumericText("Audio volume percent (0-100)", valueText, valueText, sizeof(valueText), false))
			{
				char *end = NULL;
				unsigned long value = strtoul(valueText, &end, 10);
				if (end != valueText && *end == '\0' && value <= 100)
					preferences.AudioVolume = (u32)value;
			}
		}

		UI::DrawText(12, 106, "Cache is ring memory; stretch size is DSP block.");
		UI::DrawText(12, 124, "Latency caps queued DSP audio, not the ring.");
		UI::DrawText(12, 142, "Changes apply as audio buffers are submitted.");
	}
	else if (optionsSubpage == 2)
	{
		// All remaining per-ROM boolean settings are available here. Keeping
		// these on a separate page avoids crowding the touchscreen controls.
		if (UI::DrawToggle(10, 22, 145, 32, "Patches", preferences.PatchesEnabled))
			preferences.PatchesEnabled = !preferences.PatchesEnabled;
		if (UI::DrawToggle(165, 22, 145, 32, "Dynarec", preferences.DynarecEnabled))
			preferences.DynarecEnabled = !preferences.DynarecEnabled;

		if (UI::DrawToggle(10, 59, 145, 32, "Dynarec loops", preferences.DynarecLoopOptimisation))
			preferences.DynarecLoopOptimisation = !preferences.DynarecLoopOptimisation;
		if (UI::DrawToggle(165, 59, 145, 32, "Dynarec doubles", preferences.DynarecDoublesOptimisation))
			preferences.DynarecDoublesOptimisation = !preferences.DynarecDoublesOptimisation;

		if (UI::DrawToggle(10, 96, 145, 32, "Double display", preferences.DoubleDisplayEnabled))
			preferences.DoubleDisplayEnabled = !preferences.DoubleDisplayEnabled;
		if (UI::DrawToggle(165, 96, 145, 32, "Clean scene", preferences.CleanSceneEnabled))
			preferences.CleanSceneEnabled = !preferences.CleanSceneEnabled;

		if (UI::DrawToggle(10, 133, 145, 32, "Clear depth", preferences.ClearDepthFrameBuffer))
			preferences.ClearDepthFrameBuffer = !preferences.ClearDepthFrameBuffer;
		if (UI::DrawToggle(165, 133, 145, 32, "Fog", preferences.FogEnabled))
			preferences.FogEnabled = !preferences.FogEnabled;

		if (UI::DrawToggle(10, 170, 145, 32, "Memory optimize", preferences.MemoryAccessOptimisation))
			preferences.MemoryAccessOptimisation = !preferences.MemoryAccessOptimisation;
		if (UI::DrawToggle(165, 170, 145, 32, "Cheats", preferences.CheatsEnabled))
			preferences.CheatsEnabled = !preferences.CheatsEnabled;
	}
	else if (optionsSubpage == 3)
	{
		char label[48];
		snprintf(label, sizeof(label), "Tex hash: %s",
			Preferences_GetTextureHashFrequencyDescription(preferences.CheckTextureHashFrequency));
		if (UI::DrawButton(10, 22, 145, 32, label))
			preferences.CheckTextureHashFrequency = static_cast<ETextureHashFrequency>(
				(preferences.CheckTextureHashFrequency + 1) % NUM_THF);

		snprintf(label, sizeof(label), "Zoom: %.2f", preferences.ZoomX);
		if (UI::DrawButton(165, 22, 145, 32, label))
		{
			char valueText[16];
			snprintf(valueText, sizeof(valueText), "%.2f", preferences.ZoomX);
			if (PromptNumericText("Zoom factor (0.5-1.5)", valueText, valueText, sizeof(valueText), true))
			{
				char *end = NULL;
				float value = strtof(valueText, &end);
				if (end != valueText && *end == '\0' && value >= 0.5f && value <= 1.5f)
					preferences.ZoomX = value;
			}
		}

		if (CTRMultiplayer::GetState() == CTRMultiplayer::STATE_OFF)
		{
			char hostPlayerString[24];
			snprintf(hostPlayerString, sizeof(hostPlayerString), "Host player: P%u", CTRInput_GetLocalControllerPort() + 1);
			if (UI::DrawButton(10, 59, 145, 32, hostPlayerString))
			{
				const unsigned int port = CTRInput_GetLocalControllerPort();
				CTRInput_SetLocalControllerPort((port + 1) % 4);
			}
		}
		else
			UI::DrawText(10, 80, "Host player locked during multiplayer");

		snprintf(label, sizeof(label), "N64 CPU: %u MHz", (unsigned int)preferences.N64CPUClockMHz);
		if (UI::DrawButton(10, 96, 145, 32, label))
		{
			char valueText[16];
			snprintf(valueText, sizeof(valueText), "%u", (unsigned int)preferences.N64CPUClockMHz);
			if (PromptNumericText("Reported N64 CPU MHz (1-1000)", valueText, valueText, sizeof(valueText), false))
			{
				char *end = NULL;
				unsigned long value = strtoul(valueText, &end, 10);
				if (end != valueText && *end == '\0' && value >= 1 && value <= 1000)
					preferences.N64CPUClockMHz = (u32)value;
			}
		}
		snprintf(label, sizeof(label), "N64 bus: %u MHz", (unsigned int)preferences.N64BusClockMHz);
		if (UI::DrawButton(165, 96, 145, 32, label))
		{
			char valueText[16];
			snprintf(valueText, sizeof(valueText), "%u", (unsigned int)preferences.N64BusClockMHz);
			if (PromptNumericText("Reported N64 bus MHz (1-1000)", valueText, valueText, sizeof(valueText), false))
			{
				char *end = NULL;
				unsigned long value = strtoul(valueText, &end, 10);
				if (end != valueText && *end == '\0' && value >= 1 && value <= 1000)
					preferences.N64BusClockMHz = (u32)value;
			}
		}
		UI::DrawText(12, 140, "Affects OS clock/timer reporting, not CPU throughput.");
	}
	else if (optionsSubpage == 4)
	{
		if (UI::DrawToggle(10, 22, 145, 32, "FPS display", gGlobalPreferences.DisplayFramerate != 0))
			gGlobalPreferences.DisplayFramerate = gGlobalPreferences.DisplayFramerate ? 0 : 1;
		if (UI::DrawToggle(165, 22, 145, 32, "Linear filter", gGlobalPreferences.ForceLinearFilter))
			gGlobalPreferences.ForceLinearFilter = !gGlobalPreferences.ForceLinearFilter;

		if (UI::DrawToggle(10, 59, 145, 32, "Battery warning", gGlobalPreferences.BatteryWarning))
			gGlobalPreferences.BatteryWarning = !gGlobalPreferences.BatteryWarning;
		if (UI::DrawToggle(165, 59, 145, 32, "Large ROM buffer", gGlobalPreferences.LargeROMBuffer))
			gGlobalPreferences.LargeROMBuffer = !gGlobalPreferences.LargeROMBuffer;

		if (UI::DrawToggle(10, 96, 145, 32, "Rumble pak", gGlobalPreferences.RumblePak))
			gGlobalPreferences.RumblePak = !gGlobalPreferences.RumblePak;
		if (UI::DrawToggle(165, 96, 145, 32, "TV output", gGlobalPreferences.TVEnable))
			gGlobalPreferences.TVEnable = !gGlobalPreferences.TVEnable;

		if (UI::DrawToggle(10, 133, 145, 32, "TV interlace", gGlobalPreferences.TVLaced))
			gGlobalPreferences.TVLaced = !gGlobalPreferences.TVLaced;
#ifdef DAEDALUS_DEBUG_DISPLAYLIST
		if (UI::DrawToggle(165, 133, 145, 32, "Custom blend", gGlobalPreferences.CustomBlendModes))
			gGlobalPreferences.CustomBlendModes = !gGlobalPreferences.CustomBlendModes;
		if (UI::DrawToggle(10, 170, 145, 32, "Highlight blends", gGlobalPreferences.HighlightInexactBlendModes))
			gGlobalPreferences.HighlightInexactBlendModes = !gGlobalPreferences.HighlightInexactBlendModes;
#endif
	}

	if (UI::DrawButton(10, 210, 145, 26, optionsSubpage == 0 ? "Back" : "Previous"))
	{
		if (optionsSubpage == 0)
		{
			CPreferences::Get()->Commit();
			currentPage = 0;
		}
		else
			--optionsSubpage;
	}
	if (UI::DrawButton(165, 210, 145, 26, optionsSubpage == 4 ? "Back to menu" : "Next page"))
	{
		if (optionsSubpage == 4)
		{
			CPreferences::Get()->Commit();
			currentPage = 0;
			optionsSubpage = 0;
		}
		else
			++optionsSubpage;
	}

	CPreferences::Get()->SetRomPreferences(g_ROM.mRomID, preferences);
	preferences.Apply();
}

static void DrawControllerPage()
{
	SRomPreferences preferences;
	CPreferences::Get()->GetRomPreferences(g_ROM.mRomID, &preferences);
	for (unsigned int i = 0; i < 3; ++i)
		CTRInput_SetStickDestination(i, preferences.CTRStickDestinations[i]);
	CTRInput_SetTouchStickPosition(preferences.CTRTouchStickX, preferences.CTRTouchStickY);

	UI::DrawHeader("Controller");
	UI::DrawText(12, 25, "Route each stick to a control type:");
	for (unsigned int i = 0; i < 3; ++i)
	{
		const float y = 34.0f + (float)i * 34.0f;
		UI::DrawText(14, y + 20, CTRInput_GetStickSourceName(i));
		char route[32];
		snprintf(route, sizeof(route), "-> %s", CTRInput_GetStickDestinationName(preferences.CTRStickDestinations[i]));
		if (UI::DrawButton(150, y, 160, 28, route))
		{
			preferences.CTRStickDestinations[i] = (preferences.CTRStickDestinations[i] + 1) % 4;
			CTRInput_SetStickDestination(i, preferences.CTRStickDestinations[i]);
		}
	}

	UI::DrawText(12, 137, "Touch stick position:");
	char positionButton[32];
	snprintf(positionButton, sizeof(positionButton), "Stick X: %u", (unsigned int)preferences.CTRTouchStickX);
	if (UI::DrawButton(10, 144, 145, 30, positionButton))
	{
		char valueText[16];
		snprintf(valueText, sizeof(valueText), "%u", (unsigned int)preferences.CTRTouchStickX);
		if (PromptNumericText("Touch stick X (16-304)", valueText, valueText, sizeof(valueText), false))
		{
			char *end = NULL;
			unsigned long value = strtoul(valueText, &end, 10);
			if (end != valueText && *end == '\0' && value >= CTR_TOUCH_STICK_RADIUS && value <= 320 - CTR_TOUCH_STICK_RADIUS)
				preferences.CTRTouchStickX = (u32)value;
		}
	}
	snprintf(positionButton, sizeof(positionButton), "Stick Y: %u", (unsigned int)preferences.CTRTouchStickY);
	if (UI::DrawButton(165, 144, 145, 30, positionButton))
	{
		char valueText[16];
		snprintf(valueText, sizeof(valueText), "%u", (unsigned int)preferences.CTRTouchStickY);
		if (PromptNumericText("Touch stick Y (16-224)", valueText, valueText, sizeof(valueText), false))
		{
			char *end = NULL;
			unsigned long value = strtoul(valueText, &end, 10);
			if (end != valueText && *end == '\0' && value >= CTR_TOUCH_STICK_RADIUS && value <= 240 - CTR_TOUCH_STICK_RADIUS)
				preferences.CTRTouchStickY = (u32)value;
		}
	}

	CTRInput_SetTouchStickPosition(preferences.CTRTouchStickX, preferences.CTRTouchStickY);
	unsigned int stickX, stickY;
	CTRInput_GetTouchStickPosition(&stickX, &stickY);
	UI::DrawVirtualStickPreview(258.0f, 151.0f,
		CTRInput_GetStickDestination(2) != CTR_STICK_DISABLED);
	char position[24];
	snprintf(position, sizeof(position), "X%u Y%u", stickX, stickY);
	UI::DrawText(236, 184, position);
	UI::DrawText(12, 178, "Touch stick works during gameplay.");

	if (UI::DrawButton(10, 190, 300, 38, "Back / Save"))
	{
		CPreferences::Get()->SetRomPreferences(g_ROM.mRomID, preferences);
		CPreferences::Get()->Commit();
		currentPage = 0;
	}
	CPreferences::Get()->SetRomPreferences(g_ROM.mRomID, preferences);
}

static void DrawMultiplayerPage()
{
	UI::DrawHeader("Multiplayer");

	if(UI::DrawButton(10, 22, 145, 48, "Host Local"))
		currentPage = 6;
	if(UI::DrawButton(165, 22, 145, 48, "Join Local"))
		currentPage = 7;
	if(UI::DrawButton(10, 76, 145, 48, "Host Online"))
		currentPage = 8;
	if(UI::DrawButton(165, 76, 145, 48, "Join Online"))
		currentPage = 9;
#if defined(DAEDALUS_DOWNLOADPLAY_HOST)
	if(UI::DrawButton(10, 130, 300, 42, "Download Play demo"))
		currentPage = 11;
#endif
	if(UI::DrawButton(10, 184, 300, 44, "Back"))
		currentPage = 0;
}

static void DrawHostPage()
{
	UI::DrawHeader("Host nearby room");
	UI::DrawText(14, 52, CTRMultiplayer::GetStatus());

	if (CTRMultiplayer::GetState() == CTRMultiplayer::STATE_OFF)
	{
		UI::DrawText(14, 82, "Players join over local wireless.");
		UI::DrawText(14, 100, "Run the same ROM in Daedalus on every 3DS.");
		UI::DrawText(14, 116, "Start games together; only input is synced.");
		if (UI::DrawButton(10, 136, 300, 32, "Start hosting"))
			CTRMultiplayer::Host();
	}
	else
	{
		UI::DrawText(14, 82, "Press Back to resume emulation.");
		if (UI::DrawButton(10, 112, 300, 48, "Stop session"))
			CTRMultiplayer::Stop();
	}

	if (UI::DrawButton(10, 184, 300, 44, "Back"))
		currentPage = 5;
}

static void DrawJoinPage()
{
	UI::DrawHeader("Join nearby room");
	UI::DrawText(14, 38, CTRMultiplayer::GetStatus());

	if (CTRMultiplayer::GetState() != CTRMultiplayer::STATE_OFF)
	{
		UI::DrawText(14, 68, "Stop the active session before scanning.");
		if (UI::DrawButton(10, 112, 300, 48, "Stop session"))
			CTRMultiplayer::Stop();
	}
	else
	{
		UI::DrawText(14, 38, "Load the same ROM on each 3DS first.");
		if (UI::DrawButton(10, 52, 300, 40, "Scan nearby rooms"))
			CTRMultiplayer::Scan();

		const size_t count = CTRMultiplayer::GetRoomCount();
		for (size_t i = 0; i < count && i < 3; ++i)
		{
			char roomLabel[32];
			CTRMultiplayer::GetRoomLabel(i, roomLabel, sizeof(roomLabel));
			if (UI::DrawButton(10, 100 + (int)i * 27, 300, 24, roomLabel))
				CTRMultiplayer::Join(i);
		}
	}

	if (UI::DrawButton(10, 184, 300, 44, "Back"))
		currentPage = 5;
}

#if defined(DAEDALUS_DOWNLOADPLAY_HOST)
static void DrawDownloadPlayHostPage()
{
	CTRDownloadPlayHost::Tick();
	UI::DrawHeader("Download Play demo");
	UI::DrawText(14, 38, CTRDownloadPlayHost::GetStatus());

	const CTRDownloadPlayHost::State state = CTRDownloadPlayHost::GetState();
	if (state == CTRDownloadPlayHost::STATE_OFF || state == CTRDownloadPlayHost::STATE_ERROR)
	{
		UI::DrawText(14, 72, "Open Download Play on nearby consoles.");
		UI::DrawText(14, 90, "Uses the configured child title (unverified).");
		if (UI::DrawButton(10, 112, 300, 40, "Open Download Play session"))
		{
			// DLP creates its own local UDS network; stop the emulator's existing
			// nearby/online session before asking the system service to host DLP.
			CTRMultiplayer::Stop();
			CTRDownloadPlayHost::Start();
		}
		if (state == CTRDownloadPlayHost::STATE_ERROR &&
			UI::DrawButton(10, 156, 145, 30, "Reset status"))
			CTRDownloadPlayHost::Stop();
	}
	else if (state == CTRDownloadPlayHost::STATE_ACCEPTING)
	{
		char joined[64];
		snprintf(joined, sizeof(joined), "Joined clients: %u / 4", CTRDownloadPlayHost::GetClientCount());
		UI::DrawText(14, 74, joined);
		if (UI::DrawButton(10, 108, 300, 38, "Start Download Play distribution"))
			CTRDownloadPlayHost::BeginDistribution();
		if (UI::DrawButton(10, 150, 300, 32, "Stop Download Play"))
			CTRDownloadPlayHost::Stop();
	}
	else if (state == CTRDownloadPlayHost::STATE_DISTRIBUTING)
	{
		char progress[64];
		snprintf(progress, sizeof(progress), "Reported transfer progress: %u%%",
			CTRDownloadPlayHost::GetProgressPercent());
		UI::DrawText(14, 74, progress);
		UI::DrawText(14, 94, "Please keep the host awake.");
		if (UI::DrawButton(10, 144, 300, 38, "Cancel / close session"))
			CTRDownloadPlayHost::Stop();
	}
	else if (state == CTRDownloadPlayHost::STATE_FINISHED)
	{
		UI::DrawText(14, 74, "Service reports transfer complete; verify clients.");
		UI::DrawText(14, 94, "Stop this service before leaving the menu.");
		if (UI::DrawButton(10, 136, 300, 38, "Close Download Play session"))
			CTRDownloadPlayHost::Stop();
	}

	if (UI::DrawButton(10, 190, 300, 34, "Back"))
	{
		CTRDownloadPlayHost::Stop();
		currentPage = 5;
	}
}
#endif

// Draw a touch key for the online text-entry panel. Keeping text input in the
// application avoids launching the system software-keyboard applet, which
// takes ownership of the 3DS graphics context and can leave PicaGL in a stale
// target/state when it returns.
static bool DrawOnlineKey(float x, float y, float width, const char *label,
	const touchPosition &touch, bool touchDown)
{
	const float height = 27.0f;
	const bool pressed = touchDown && touch.px >= x && touch.px < x + width &&
		touch.py >= y && touch.py < y + height;

	glDisable(GL_TEXTURE_2D);
	glColor3f(pressed ? 0.1f : 0.15f, pressed ? 0.6f : 0.5f,
		pressed ? 0.5f : 0.75f);
	glBegin(GL_TRIANGLE_STRIP);
		glVertex2f(x, y);
		glVertex2f(x + width, y);
		glVertex2f(x, y + height);
		glVertex2f(x + width, y + height);
	glEnd();

	const float labelWidth = (float)strlen(label) * 8.0f;
	glColor3f(0.95f, 0.95f, 0.95f);
	UI::DrawText(x + (width - labelWidth) * 0.5f, y + 19.0f, label);
	return pressed;
}

static bool PromptNumericText(const char *hint, const char *initialValue,
	char *buffer, size_t bufferSize, bool allowDecimal)
{
	if (buffer == NULL || bufferSize == 0)
		return false;
	char initialCopy[64] = {};
	if (initialValue != NULL)
		snprintf(initialCopy, sizeof(initialCopy), "%s", initialValue);
	snprintf(buffer, bufferSize, "%s", initialCopy);
	size_t length = strlen(buffer);
	bool accepted = false;
	bool cancelled = false;

	while (aptMainLoop() && !accepted && !cancelled)
	{
		hidScanInput();
		UI::RestoreRenderState();
		glClear(GL_COLOR_BUFFER_BIT);
		UI::DrawHeader("Number entry");
		UI::DrawText(8, 37, hint);
		UI::DrawText(18, 58, buffer);

		touchPosition touch;
		hidTouchRead(&touch);
		const bool touchDown = (hidKeysDown() & KEY_TOUCH) != 0;
		bool handled = false;
		static const char *const digitRows[] = { "123", "456", "789" };
		for (unsigned int row = 0; row < 3; ++row)
		{
			for (unsigned int col = 0; col < 3; ++col)
			{
				char digit[2] = { digitRows[row][col], '\0' };
				if (DrawOnlineKey(74.0f + col * 58.0f, 74.0f + row * 31.0f,
					52.0f, digit, touch, touchDown) && !handled)
				{
					if (length + 1 < bufferSize)
					{
						buffer[length++] = digit[0];
						buffer[length] = '\0';
					}
					handled = true;
				}
			}
		}
		if (DrawOnlineKey(132.0f, 167.0f, 52.0f, "0", touch, touchDown) && !handled)
		{
			if (length + 1 < bufferSize)
			{
				buffer[length++] = '0';
				buffer[length] = '\0';
			}
			handled = true;
		}
		if (allowDecimal && DrawOnlineKey(74.0f, 167.0f, 52.0f, ".", touch, touchDown) && !handled)
		{
			if (length + 1 < bufferSize && strchr(buffer, '.') == NULL)
			{
				buffer[length++] = '.';
				buffer[length] = '\0';
			}
			handled = true;
		}
		if (DrawOnlineKey(190.0f, 167.0f, 52.0f, "<", touch, touchDown) && !handled)
		{
			if (length > 0) buffer[--length] = '\0';
			handled = true;
		}
		if (DrawOnlineKey(8.0f, 202.0f, 88.0f, "Cancel", touch, touchDown) && !handled)
		{
			cancelled = true;
			handled = true;
		}
		if (DrawOnlineKey(100.0f, 202.0f, 120.0f, "Clear", touch, touchDown) && !handled)
		{
			length = 0;
			buffer[0] = '\0';
			handled = true;
		}
		if (DrawOnlineKey(224.0f, 202.0f, 88.0f, "Enter", touch, touchDown) && !handled)
		{
			accepted = length != 0;
			handled = true;
		}
		pglSwapBuffers();
	}
	UI::RestoreRenderState();
	return accepted;
}

static bool PromptOnlineText(const char *hint, char *buffer, size_t bufferSize)
{
	static const char *const rows[] = { "1234567890", "qwertyuiop", "asdfghjkl" };
	if (buffer == NULL || bufferSize == 0)
		return false;

	buffer[0] = '\0';
	size_t length = 0;
	bool upperCase = strstr(hint, "6-character") != NULL;
	bool accepted = false;
	bool cancelled = false;

	while (aptMainLoop() && !accepted && !cancelled)
	{
		hidScanInput();
		UI::RestoreRenderState();
		glClear(GL_COLOR_BUFFER_BIT);
		UI::DrawHeader("Online text entry");
		UI::DrawText(8, 35, hint);

		// Keep long addresses readable by showing the tail as it is entered.
		const size_t first = length > 37 ? length - 37 : 0;
		char visibleText[40];
		snprintf(visibleText, sizeof(visibleText), "%s", buffer + first);
		UI::DrawText(8, 53, visibleText);

		touchPosition touch;
		hidTouchRead(&touch);
		const bool touchDown = (hidKeysDown() & KEY_TOUCH) != 0;
		bool handled = false;

		for (size_t row = 0; row < 3; ++row)
		{
			const char *keys = rows[row];
			const float keyWidth = row == 2 ? 32.0f : 30.0f;
			const float xStart = row == 2 ? 16.0f : 10.0f;
			const float y = 62.0f + (float)row * 31.0f;
			for (size_t key = 0; keys[key] != '\0'; ++key)
			{
				char label[2] = { keys[key], '\0' };
				if (DrawOnlineKey(xStart + key * keyWidth, y, keyWidth - 2.0f,
					label, touch, touchDown) && !handled)
				{
					if (length + 1 < bufferSize)
					{
					char value = keys[key];
					if (upperCase && value >= 'a' && value <= 'z') value -= 'a' - 'A';
					buffer[length++] = value;
					buffer[length] = '\0';
				}
				handled = true;
				}
			}
		}

		// Punctuation row supports host:port, IPv4/IPv6, and DNS names.
		const char *punctuation = "zxcvbnm-.:@";
		for (size_t key = 0; punctuation[key] != '\0'; ++key)
		{
			char label[2] = { punctuation[key], '\0' };
			if (DrawOnlineKey(4.0f + key * 25.0f, 155.0f, 24.0f,
				label, touch, touchDown) && !handled)
			{
				if (length + 1 < bufferSize)
				{
					char value = punctuation[key];
					if (upperCase && value >= 'a' && value <= 'z') value -= 'a' - 'A';
					buffer[length++] = value;
					buffer[length] = '\0';
				}
				handled = true;
			}
		}
		if (DrawOnlineKey(279.0f, 155.0f, 37.0f, upperCase ? "ABC" : "abc",
			touch, touchDown) && !handled)
		{
			upperCase = !upperCase;
			handled = true;
		}

		if (DrawOnlineKey(8.0f, 190.0f, 62.0f, "Cancel", touch, touchDown) && !handled)
		{
			cancelled = true;
			handled = true;
		}
		if (DrawOnlineKey(74.0f, 190.0f, 72.0f, "Space", touch, touchDown) && !handled)
		{
			if (length + 1 < bufferSize)
			{
				buffer[length++] = ' ';
				buffer[length] = '\0';
			}
			handled = true;
		}
		if (DrawOnlineKey(150.0f, 190.0f, 62.0f, "Delete", touch, touchDown) && !handled)
		{
			if (length > 0) buffer[--length] = '\0';
			handled = true;
		}
		if (DrawOnlineKey(216.0f, 190.0f, 96.0f, "Enter", touch, touchDown) && !handled)
		{
			accepted = true;
			handled = true;
		}

		pglSwapBuffers();
	}

	// The modal panel and its text are rendered in-process, but restore the
	// regular menu target/state before returning to the caller regardless.
	UI::RestoreRenderState();
	return accepted && buffer[0] != '\0';
}

static void DrawOnlineInfoPage(bool host)
{
	UI::DrawHeader(host ? "Host online" : "Join online");
	UI::DrawText(14, 38, CTRMultiplayer::GetStatus());

	if (CTRMultiplayer::GetState() != CTRMultiplayer::STATE_OFF)
	{
		if (host)
		{
			UI::DrawText(14, 66, "Share code, public address and TCP port.");
			UI::DrawText(14, 86, "Forward the port to this 3DS on your router.");
		}
		else
			UI::DrawText(14, 66, "Online input relay is connected.");
		if (UI::DrawButton(10, 112, 300, 48, "Stop session"))
			CTRMultiplayer::Stop();
	}
	else
	{
		if (host)
		{
			UI::DrawText(14, 68, "This 3DS runs the online relay.");
			UI::DrawText(14, 88, "Forward its TCP port on your router.");
			if (UI::DrawButton(10, 112, 300, 48, "Start 3DS online host"))
			{
				char listenPort[16];
				if (PromptOnlineText("TCP listen port (enter 37777)", listenPort, sizeof(listenPort)))
					CTRMultiplayer::HostOnline(listenPort);
			}
		}
		else
		{
			UI::DrawText(14, 68, "Enter the 3DS host address:port.");
			UI::DrawText(14, 88, "The host's router must forward TCP.");
			if (UI::DrawButton(10, 112, 300, 48, "Enter host and room code"))
			{
				char serverAddress[64];
				if (PromptOnlineText("Host hostname or IPv4:port", serverAddress, sizeof(serverAddress)))
				{
					char roomCode[16];
					if (PromptOnlineText("Enter the 6-character room code", roomCode, sizeof(roomCode)))
						CTRMultiplayer::JoinOnline(serverAddress, roomCode);
				}
			}
		}
	}

	if (UI::DrawButton(10, 184, 300, 44, "Back"))
		currentPage = 5;
}

static void DrawMainPage()
{
	char titleString[20];

	sprintf(titleString, "FPS: %.2f", gCurrentFramerate);
	UI::DrawHeader(titleString);

	if((osGetTime() - timer) > 5000)
	{
		const bool fastForwardEnabled = FramerateLimiter_IsFastForwardEnabled();
		if (UI::DrawToggle(CTR_FAST_FORWARD_BUTTON_X, CTR_FAST_FORWARD_BUTTON_Y,
			CTR_FAST_FORWARD_BUTTON_WIDTH, CTR_FAST_FORWARD_BUTTON_HEIGHT,
			fastForwardEnabled ? "FF: ON" : "FF: OFF", fastForwardEnabled))
			FramerateLimiter_SetFastForward(!fastForwardEnabled);

		if (CTRInput_GetStickDestination(2) != CTR_STICK_DISABLED)
		{
			unsigned int x, y;
			CTRInput_GetTouchStickPosition(&x, &y);
			UI::DrawVirtualStick((float)x, (float)y, (keysHeld() & KEY_TOUCH) != 0);
		}
		touchPosition touch;
		hidTouchRead(&touch);
		const int dx = (int)touch.px;
		const int dy = (int)touch.py;
		unsigned int stickX, stickY;
		CTRInput_GetTouchStickPosition(&stickX, &stickY);
		const int sx = dx - (int)stickX;
		const int sy = dy - (int)stickY;
		const bool usingVirtualStick = (keysHeld() & KEY_TOUCH) &&
			CTRInput_GetStickDestination(2) != CTR_STICK_DISABLED && sx * sx + sy * sy <= CTR_TOUCH_STICK_RADIUS * CTR_TOUCH_STICK_RADIUS;
		const bool usingFastForwardButton = (keysHeld() & KEY_TOUCH) &&
			touch.px >= CTR_FAST_FORWARD_BUTTON_X &&
			touch.px < CTR_FAST_FORWARD_BUTTON_X + CTR_FAST_FORWARD_BUTTON_WIDTH &&
			touch.py >= CTR_FAST_FORWARD_BUTTON_Y &&
			touch.py < CTR_FAST_FORWARD_BUTTON_Y + CTR_FAST_FORWARD_BUTTON_HEIGHT;
		if ((keysHeld() & KEY_TOUCH) && !usingVirtualStick && !usingFastForwardButton)
			timer = osGetTime();
		return;
	}

	if(UI::DrawButton(10,  22, 145, 48, "Save State")) currentPage = 1;
	if(UI::DrawButton(165, 22, 145, 48, "Load State")) currentPage = 2;
	if(UI::DrawButton(10,  76, 145, 48, "Multiplayer")) currentPage = 5;
	if(UI::DrawButton(165, 76, 145, 48, "Options")) currentPage = 4;
	if(UI::DrawButton(10,  130, 145, 48, "Controller")) currentPage = 10;
	if(UI::DrawButton(165, 130, 145, 48, "Close ROM")) currentPage = 3;
}

void UI::DrawInGameMenu()
{
	UI::RestoreRenderState();
	glClear(GL_COLOR_BUFFER_BIT);

	switch(currentPage)
	{
		case 0: DrawMainPage(); break;
		case 1: DrawSaveStatePage(); break;
		case 2: DrawLoadStatePage(); break;
		case 3: DrawConfirmPage(); break;
		case 4: DrawOptionsPage(); break;
		case 5: DrawMultiplayerPage(); break;
		case 6: DrawHostPage(); break;
		case 7: DrawJoinPage(); break;
		case 8: DrawOnlineInfoPage(true); break;
		case 9: DrawOnlineInfoPage(false); break;
		case 10: DrawControllerPage(); break;
#if defined(DAEDALUS_DOWNLOADPLAY_HOST)
		case 11: DrawDownloadPlayHostPage(); break;
#endif
	}

	pglSwapBuffers();
	pglSelectScreen(GFX_TOP, GFX_LEFT);
}