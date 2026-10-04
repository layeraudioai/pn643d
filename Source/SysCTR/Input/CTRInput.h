#ifndef SYSCTR_INPUT_CTRINPUT_H
#define SYSCTR_INPUT_CTRINPUT_H

// Select which N64 controller port receives this console's physical input.
// Port indices are 0..3 (N64 controllers 1..4).
unsigned int CTRInput_GetLocalControllerPort();
void CTRInput_SetLocalControllerPort(unsigned int port);

// Per-source destination routing. Sources are Circle Pad, C-Stick and the
// touch-screen virtual stick; destinations are N64 analog, D-pad or C-buttons.
enum ECTRStickDestination
{
    CTR_STICK_ANALOG = 0,
    CTR_STICK_DPAD = 1,
    CTR_STICK_CBUTTONS = 2,
    CTR_STICK_DISABLED = 3,
};
unsigned int CTRInput_GetStickDestination(unsigned int source);
void CTRInput_SetStickDestination(unsigned int source, unsigned int destination);
void CTRInput_GetTouchStickPosition(unsigned int *x, unsigned int *y);
void CTRInput_SetTouchStickPosition(unsigned int x, unsigned int y);
const char *CTRInput_GetStickSourceName(unsigned int source);
const char *CTRInput_GetStickDestinationName(unsigned int destination);
void CTRInput_ApplyTouchStick(unsigned int heldKeys, int touchX, int touchY, int *x, int *y);

#endif
