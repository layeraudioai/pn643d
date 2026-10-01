#ifndef SYSCTR_INPUT_CTRINPUT_H
#define SYSCTR_INPUT_CTRINPUT_H

// Select which N64 controller port receives this console's physical input.
// Port indices are 0..3 (N64 controllers 1..4).
unsigned int CTRInput_GetLocalControllerPort();
void CTRInput_SetLocalControllerPort(unsigned int port);

#endif
