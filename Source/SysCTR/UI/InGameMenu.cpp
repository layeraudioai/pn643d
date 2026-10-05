#include <3ds.h>
#include <GL/picaGL.h>
#include <stdio.h>
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
#include "Utility/IO.h"
#include "Utility/Preferences.h"

extern uint8_t aspectRatio;
extern float gCurrentFramerate;
extern RomInfo g_ROM;

static uint64_t timer;
static uint8_t currentPage = 0;
static uint8_t optionsSubpage = 0;

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
	snprintf(header, sizeof(header), "Options %u/4", (unsigned int)optionsSubpage + 1);
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
			preferences.MaxFPS = preferences.MaxFPS > 420.0f ? 40.0f : preferences.MaxFPS + 5.0f;

		if (UI::DrawToggle(10, 133, 145, 32, "Aspect ratio", aspectRatio != 0))
			aspectRatio = !aspectRatio;
		if (UI::DrawToggle(165, 133, 145, 32, "Speed sync", preferences.SpeedSyncEnabled != 0))
			preferences.SpeedSyncEnabled = preferences.SpeedSyncEnabled ? 0 : 1;

		snprintf(label, sizeof(label), "Stereo: %.3f", preferences.StereoSeparation);
		if (UI::DrawButton(10, 170, 145, 32, label))
		{
			preferences.StereoSeparation += 0.025f;
			if (preferences.StereoSeparation > 0.2001f) preferences.StereoSeparation = 0.0f;
		}
		if (UI::DrawToggle(165, 170, 145, 32,
			preferences.StereoPopout ? "3D: Pop-out" : "3D: Depth", preferences.StereoPopout))
			preferences.StereoPopout = !preferences.StereoPopout;
	}
	else if (optionsSubpage == 1)
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
	else if (optionsSubpage == 2)
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
			preferences.ZoomX += 0.05f;
			if (preferences.ZoomX > 1.5001f) preferences.ZoomX = 0.50f;
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
	}
	else
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
	if (UI::DrawButton(165, 210, 145, 26, optionsSubpage == 3 ? "Back to menu" : "Next page"))
	{
		if (optionsSubpage == 3)
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
	if (UI::DrawButton(10, 144, 42, 30, "<"))
		preferences.CTRTouchStickX = preferences.CTRTouchStickX > CTR_TOUCH_STICK_RADIUS + 12 ? preferences.CTRTouchStickX - 12 : CTR_TOUCH_STICK_RADIUS;
	if (UI::DrawButton(56, 144, 42, 30, "^"))
		preferences.CTRTouchStickY = preferences.CTRTouchStickY > CTR_TOUCH_STICK_RADIUS + 12 ? preferences.CTRTouchStickY - 12 : CTR_TOUCH_STICK_RADIUS;
	if (UI::DrawButton(102, 144, 42, 30, "v"))
		preferences.CTRTouchStickY = preferences.CTRTouchStickY < 240 - CTR_TOUCH_STICK_RADIUS - 12 ? preferences.CTRTouchStickY + 12 : 240 - CTR_TOUCH_STICK_RADIUS;
	if (UI::DrawButton(148, 144, 42, 30, ">"))
		preferences.CTRTouchStickX = preferences.CTRTouchStickX < 320 - CTR_TOUCH_STICK_RADIUS - 12 ? preferences.CTRTouchStickX + 12 : 320 - CTR_TOUCH_STICK_RADIUS;

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
		if (UI::DrawButton(10, 112, 300, 48, "Start hosting"))
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

static bool PromptOnlineText(const char *hint, char *buffer, size_t bufferSize)
{
	SwkbdState keyboard;
	swkbdInit(&keyboard, SWKBD_TYPE_WESTERN, 2, -1);
	swkbdSetHintText(&keyboard, hint);
	swkbdSetFeatures(&keyboard, SWKBD_DEFAULT_QWERTY);
	buffer[0] = '\0';
	const SwkbdButton result = swkbdInputText(&keyboard, buffer, bufferSize);

	// The system keyboard runs as an applet and may leave PicaGL's target,
	// viewport, matrices, and fixed-function state changed. Reassert the UI
	// state before the menu continues drawing; subsequent game rendering has
	// its own renderer-state reset.
	UI::RestoreRenderState();

	return result == SWKBD_BUTTON_RIGHT && buffer[0] != '\0';
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
		if ((keysHeld() & KEY_TOUCH) && !usingVirtualStick)
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
	}

	pglSwapBuffers();
	pglSelectScreen(GFX_TOP, GFX_LEFT);
}