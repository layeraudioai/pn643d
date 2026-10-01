#include <3ds.h>
#include <GL/picaGL.h>
#include <stdio.h>

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

		if(UI::DrawButton(10, 22 + (54 * i), 300, 44, buttonString))
		{
			ExecSaveState(i);
		}
	}

	if(UI::DrawButton(10, 184, 300, 44, "Back"))
		currentPage = 0;
}

static void DrawLoadStatePage()
{
	char buttonString[20];

	UI::DrawHeader("Load state");

	for(int i = 0; i < 3; i++)
	{
		sprintf(buttonString, "Load slot: %i", i);

		if(UI::DrawButton(10, 22 + (54 * i), 300, 44, buttonString))
		{
			LoadSaveState(i);
		}
	}

	if(UI::DrawButton(10, 184, 300, 44, "Back"))
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

static void DrawOnlineInfoPage(const char * title)
{
	UI::DrawHeader(title);
	UI::DrawText(14, 60, "Online rooms are not supported.");
	UI::DrawText(14, 84, "Use Host/Join Local on nearby 3DS systems.");
	if(UI::DrawButton(10, 184, 300, 44, "Back"))
		currentPage = 5;
}

static void DrawMainPage()
{
	char titleString[20];

	sprintf(titleString, "FPS: %.2f", gCurrentFramerate);
	UI::DrawHeader(titleString);

	if((osGetTime() - timer) > 5000)
	{
		if(keysHeld() & KEY_TOUCH)
		{
			timer = osGetTime();
		}
		return;
	}

	if(UI::DrawButton(10,  22, 145, 48, "Save State")) currentPage = 1;
	if(UI::DrawButton(165, 22, 145, 48, "Load State")) currentPage = 2;
	if(UI::DrawButton(10,  76, 145, 48, "Multiplayer")) currentPage = 5;
	if(UI::DrawButton(165, 76, 145, 48, "Options")) currentPage = 4;
	if(UI::DrawButton(10,  130, 300, 62, "Close ROM")) currentPage = 3;
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
		case 8: DrawOnlineInfoPage("Host online"); break;
		case 9: DrawOnlineInfoPage("Join online"); break;
	}

	pglSwapBuffers();
	pglSelectScreen(GFX_TOP, GFX_LEFT);
}