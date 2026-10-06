#ifndef SYSCTR_INPUT_CTRINPUT_H
#define SYSCTR_INPUT_CTRINPUT_H

// Keep the virtual stick comfortably easy to hit on the 3DS touchscreen. Its
// hit radius, rendering and any configurable position limits share this value.
#define CTR_TOUCH_STICK_RADIUS 96

// Keep the in-game fast-forward HUD hitbox reserved from the virtual stick.
#define CTR_FAST_FORWARD_BUTTON_X 236
#define CTR_FAST_FORWARD_BUTTON_Y 15
#define CTR_FAST_FORWARD_BUTTON_WIDTH 78
#define CTR_FAST_FORWARD_BUTTON_HEIGHT 28

// Select which N64 controller port receives this console's physical input.
// Port indices are 0..3 (N64 controllers 1..4). Assignment 4 is the special
// GoldenEye-style dual-controller mode: N64 ports 1+2 act as one local player.
enum ECTRControllerAssignment
{
    CTR_CONTROLLER_P1 = 0,
    CTR_CONTROLLER_P2 = 1,
    CTR_CONTROLLER_P3 = 2,
    CTR_CONTROLLER_P4 = 3,
    CTR_CONTROLLER_P1_P2 = 4,
    CTR_CONTROLLER_ASSIGNMENT_COUNT = 5,
};
unsigned int CTRInput_GetLocalControllerPort();
unsigned int CTRInput_GetLocalControllerAssignment();
unsigned int CTRInput_GetLocalControllerPortMask();
void CTRInput_SetLocalControllerPort(unsigned int port);
void CTRInput_SetLocalControllerAssignment(unsigned int assignment);

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
