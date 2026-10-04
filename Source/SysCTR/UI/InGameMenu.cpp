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
	SRomPreferences	preferences;

	CPreferences::Get()->GetRomPreferences( g_ROM.mRomID, &preferences );

	char frameskipString[30];
	char framerateString[30];
	sprintf(frameskipString, "Frameskip: %s", Preferences_GetFrameskipDescription( preferences.Frameskip ));
	sprintf(framerateString, "Max FPS: %.0f", preferences.MaxFPS);
	char stereoString[30];
	sprintf(stereoString, "Stereo: %.3f", preferences.StereoSeparation);
	char hostPlayerString[24];
	sprintf(hostPlayerString, "Host Player: P%u", CTRInput_GetLocalControllerPort() + 1);
	UI::DrawHeader("Options");

	if(UI::DrawToggle(10,  22, 145, 48, "Toggle Audio", preferences.AudioEnabled == APM_ENABLED_ASYNC))
	{
		preferences.AudioEnabled = (preferences.AudioEnabled == APM_ENABLED_ASYNC ? APM_DISABLED : APM_ENABLED_ASYNC);
		preferences.SpeedSyncEnabled = (preferences.AudioEnabled == APM_ENABLED_ASYNC ? false : true);
	}

	if(UI::DrawButton(165,  22, 145, 48, "Aspect Ratio"))
	{
		aspectRatio = !aspectRatio;
	}

	if(UI::DrawButton(10,  76, 145, 48, frameskipString))
	{
		preferences.Frameskip = (EFrameskipValue) (preferences.Frameskip + 1);

		if(preferences.Frameskip > FV_2)
			preferences.Frameskip = FV_DISABLED;
	}

	if(UI::DrawButton(165,  76, 145, 48, framerateString))
	{
		if(preferences.MaxFPS > 420.0f)
			preferences.MaxFPS = 40.0f;
		else
			preferences.MaxFPS += 5.0f;
	}

	if(UI::DrawButton(10, 130, 145, 48, stereoString))
	{
		preferences.StereoSeparation += 0.025f;
		if(preferences.StereoSeparation > 0.2001f)
			preferences.StereoSeparation = 0.0f;
	}

	if(UI::DrawButton(165, 130, 145, 48,
		preferences.StereoPopout ? "3D: Pop-out" : "3D: Depth"))
	{
		preferences.StereoPopout = !preferences.StereoPopout;
	}

	if (CTRMultiplayer::GetState() == CTRMultiplayer::STATE_OFF)
	{
		if(UI::DrawButton(10, 184, 145, 44, hostPlayerString))
		{
			unsigned int port = CTRInput_GetLocalControllerPort();
			CTRInput_SetLocalControllerPort((port + 1) % 4);
		}
	}
	else
	{
		UI::DrawText(10, 212, "Port locked");
	}

	if(UI::DrawButton(165, 184, 145, 44, "Back"))
	{
		CPreferences::Get()->Commit();
		currentPage = 0;
	}
	
	CPreferences::Get()->SetRomPreferences( g_ROM.mRomID, preferences );

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
		preferences.CTRTouchStickX = preferences.CTRTouchStickX > 68 ? preferences.CTRTouchStickX - 12 : 56;
	if (UI::DrawButton(56, 144, 42, 30, "^"))
		preferences.CTRTouchStickY = preferences.CTRTouchStickY > 68 ? preferences.CTRTouchStickY - 12 : 56;
	if (UI::DrawButton(102, 144, 42, 30, "v"))
		preferences.CTRTouchStickY = preferences.CTRTouchStickY < 172 ? preferences.CTRTouchStickY + 12 : 184;
	if (UI::DrawButton(148, 144, 42, 30, ">"))
		preferences.CTRTouchStickX = preferences.CTRTouchStickX < 252 ? preferences.CTRTouchStickX + 12 : 264;

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
	return swkbdInputText(&keyboard, buffer, bufferSize) == SWKBD_BUTTON_RIGHT && buffer[0] != '\0';
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
			CTRInput_GetStickDestination(2) != CTR_STICK_DISABLED && sx * sx + sy * sy <= 56 * 56;
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