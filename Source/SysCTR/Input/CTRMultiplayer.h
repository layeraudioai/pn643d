#ifndef SYSCTR_INPUT_CTRMULTIPLAYER_H
#define SYSCTR_INPUT_CTRMULTIPLAYER_H

#include "OSHLE/ultra_os.h"
#include <stddef.h>

namespace CTRMultiplayer
{
    enum State
    {
        STATE_OFF,
        STATE_HOSTING,
        STATE_JOINED
    };

    // Start/stop a nearby wireless session. Scan results remain valid until the
    // next Scan() call or Stop().
    bool Host();
    bool Scan();
    size_t GetRoomCount();
    void GetRoomLabel(size_t roomIndex, char *buffer, size_t bufferSize);
    bool Join(size_t roomIndex);
    void Stop();

    State GetState();
    const char *GetStatus();

    // Called for each emulator controller poll. Network packets carry only
    // controller state; emulation and ROM data stay local to every console.
    void Update(const OSContPad localPad[4], OSContPad outputPads[4]);
}

#endif
