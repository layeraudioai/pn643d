/*
Copyright (C) 2012 StrmnNrmn

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
#include "Input/InputManager.h"
#include "SysCTR/Input/CTRInput.h"
#include "SysCTR/Input/CTRMultiplayer.h"

#include <3ds.h>

static unsigned int gLocalControllerPort = 0;
static unsigned int gStickDestinations[3] = { CTR_STICK_ANALOG, CTR_STICK_CBUTTONS, CTR_STICK_ANALOG };
static unsigned int gTouchStickX = 78;
static unsigned int gTouchStickY = 164;

unsigned int CTRInput_GetStickDestination(unsigned int source)
{
    return source < 3 ? gStickDestinations[source] : CTR_STICK_DISABLED;
}

void CTRInput_SetStickDestination(unsigned int source, unsigned int destination)
{
    if (source < 3 && destination <= CTR_STICK_DISABLED)
        gStickDestinations[source] = destination;
}

void CTRInput_GetTouchStickPosition(unsigned int *x, unsigned int *y)
{
    if (x) *x = gTouchStickX;
    if (y) *y = gTouchStickY;
}

void CTRInput_SetTouchStickPosition(unsigned int x, unsigned int y)
{
    gTouchStickX = x < 56 ? 56 : (x > 264 ? 264 : x);
    gTouchStickY = y < 56 ? 56 : (y > 184 ? 184 : y);
}

const char *CTRInput_GetStickSourceName(unsigned int source)
{
    static const char *names[] = { "Circle Pad", "C-Stick", "Touch Stick" };
    return source < 3 ? names[source] : "Stick";
}

const char *CTRInput_GetStickDestinationName(unsigned int destination)
{
    static const char *names[] = { "Analog", "D-pad", "C-buttons", "Off" };
    return destination < 4 ? names[destination] : "Off";
}

void CTRInput_ApplyTouchStick(unsigned int heldKeys, int touchX, int touchY, int *x, int *y)
{
    *x = 0;
    *y = 0;
    if (!(heldKeys & KEY_TOUCH)) return;

    const int dx = touchX - (int)gTouchStickX;
    const int dy = (int)gTouchStickY - touchY;
    const int radius = 56;
    const int distanceSquared = dx * dx + dy * dy;
    if (distanceSquared > radius * radius) return;

    *x = dx * 127 / radius;
    *y = dy * 127 / radius;
    if (*x > 127) *x = 127;
    if (*x < -127) *x = -127;
    if (*y > 127) *y = 127;
    if (*y < -127) *y = -127;
    if (*x > -10 && *x < 10) *x = 0;
    if (*y > -10 && *y < 10) *y = 0;
}

unsigned int CTRInput_GetLocalControllerPort()
{
	return gLocalControllerPort;
}

void CTRInput_SetLocalControllerPort(unsigned int port)
{
	if (port < 4)
		gLocalControllerPort = port;
}

class IInputManager : public CInputManager
{
public:
	virtual bool				Initialise();
	virtual void				Finalise();

	virtual void				GetState( OSContPad pPad[4] );

	virtual u32					GetNumConfigurations() const;
	virtual const char *		GetConfigurationName( u32 ) const;
	virtual const char *		GetConfigurationDescription( u32 ) const;
	virtual void				SetConfiguration( u32 );
	virtual u32					GetConfigurationFromName( const char * ) const;
};

bool IInputManager::Initialise()
{
	return true;
}

void IInputManager::Finalise()
{
}

void IInputManager::GetState( OSContPad pPad[4] )
{
	circlePosition circlepad, cstick;
	touchPosition touch;

	for(u32 cont = 0; cont < 4; cont++)
	{
		pPad[cont].button = 0;
		pPad[cont].stick_x = 0;
		pPad[cont].stick_y = 0;
	}

	hidScanInput();
	const u32 heldKeys = hidKeysHeld();
	hidCircleRead(&circlepad);
	hidCstickRead(&cstick);
	hidTouchRead(&touch);

	OSContPad localPads[4];
	for (u32 i = 0; i < 4; ++i)
	{
		localPads[i].button = 0;
		localPads[i].stick_x = 0;
		localPads[i].stick_y = 0;
	}
	OSContPad &localPad = localPads[gLocalControllerPort];

	int sourceX[3] = { circlepad.dx / 2, cstick.dx / 2, 0 };
	int sourceY[3] = { circlepad.dy / 2, cstick.dy / 2, 0 };
	int analogX = 0, analogY = 0;
	CTRInput_ApplyTouchStick(heldKeys, touch.px, touch.py, &sourceX[2], &sourceY[2]);

	for (unsigned int source = 0; source < 3; ++source)
	{
		const int x = sourceX[source];
		const int y = sourceY[source];
		switch (gStickDestinations[source])
		{
			case CTR_STICK_ANALOG:
				analogX += x;
				analogY += y;
				break;
			case CTR_STICK_DPAD:
				if (y > 30) localPad.button |= U_JPAD;
				if (y < -30) localPad.button |= D_JPAD;
				if (x < -30) localPad.button |= L_JPAD;
				if (x > 30) localPad.button |= R_JPAD;
				break;
			case CTR_STICK_CBUTTONS:
				if (y > 30) localPad.button |= U_CBUTTONS;
				if (y < -30) localPad.button |= D_CBUTTONS;
				if (x < -30) localPad.button |= L_CBUTTONS;
				if (x > 30) localPad.button |= R_CBUTTONS;
				break;
			default:
				break;
		}
	}
	if (analogX > 127) analogX = 127;
	if (analogX < -127) analogX = -127;
	if (analogY > 127) analogY = 127;
	if (analogY < -127) analogY = -127;
	localPad.stick_x = (s8)analogX;
	localPad.stick_y = (s8)analogY;

	if (heldKeys & KEY_A) localPad.button |= A_BUTTON;
	if (heldKeys & KEY_B) localPad.button |= B_BUTTON;
	if (heldKeys & (KEY_X | KEY_ZR | KEY_ZL)) localPad.button |= Z_TRIG;
	if (heldKeys & KEY_L) localPad.button |= L_TRIG;
	if (heldKeys & KEY_R) localPad.button |= R_TRIG;
	if (heldKeys & KEY_START) localPad.button |= START_BUTTON;

	// Keep the physical D-pad behavior as a convenient fallback.
	if (heldKeys & KEY_DUP) localPad.button |= U_JPAD;
	if (heldKeys & KEY_DDOWN) localPad.button |= D_JPAD;
	if (heldKeys & KEY_DLEFT) localPad.button |= L_JPAD;
	if (heldKeys & KEY_DRIGHT) localPad.button |= R_JPAD;
	CTRMultiplayer::Update(localPads, pPad);
}

template<> bool	CSingleton< CInputManager >::Create()
{
	DAEDALUS_ASSERT_Q(mpInstance == NULL);

	IInputManager * manager = new IInputManager();

	if(manager->Initialise())
	{
		mpInstance = manager;
		return true;
	}

	delete manager;
	return false;
}

u32	 IInputManager::GetNumConfigurations() const
{
	return 0;
}

const char * IInputManager::GetConfigurationName( u32 ) const
{
	DAEDALUS_ERROR( "Invalid controller config" );
	return "?";
}

const char * IInputManager::GetConfigurationDescription( u32 ) const
{
	DAEDALUS_ERROR( "Invalid controller config" );
	return "?";
}

void IInputManager::SetConfiguration( u32 )
{
	DAEDALUS_ERROR( "Invalid controller config" );
}

u32		IInputManager::GetConfigurationFromName( const char * ) const
{
	// Return the default controller config
	return 0;
}
