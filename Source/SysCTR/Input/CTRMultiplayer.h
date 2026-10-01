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
        STATE_JOINED,
        STATE_ONLINE_HOSTING,
        STATE_ONLINE_JOINED
    };

    // Start/stop a nearby UDS session. Scan results remain valid until the next
    // Scan() call or Stop().
    bool Host();
    bool Scan();
    size_t GetRoomCount();
    void GetRoomLabel(size_t roomIndex, char *buffer, size_t bufferSize);
    bool Join(size_t roomIndex);

    // Internet sessions use the bundled TCP relay server. The server address
    // accepts a hostname or IPv4 address, optionally followed by :port.
    bool HostOnline(const char *serverAddress);
    bool JoinOnline(const char *serverAddress, const char *roomCode);
    const char *GetOnlineRoomCode();
    bool IsOnline();

    void Stop();

    State GetState();
    const char *GetStatus();

    // Called for each emulator controller poll. Network packets carry only
    // controller state; emulation and ROM data stay local to every console.
    void Update(const OSContPad localPad[4], OSContPad outputPads[4]);
}

#endif
