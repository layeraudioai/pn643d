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
        STATE_ONLINE_JOINED,
        STATE_ONLINE_MATCHMAKING
    };

    // Start/stop a nearby UDS session. Hosts always publish a generated room
    // code and are automatically available to matchmaking. Scan results remain
    // valid until the next Scan() call or Stop().
    bool Host();
    const char *GetLocalRoomCode();
    bool Scan();
    bool JoinLocalByCode(const char *roomCode);
    bool JoinLocalMatchmaking();
    size_t GetRoomCount();
    void GetRoomLabel(size_t roomIndex, char *buffer, size_t bufferSize);
    bool Join(size_t roomIndex);

    // Direct online sessions carry controller packets between consoles. The
    // host listens on the selected port and always creates a room code. A
    // configured relay is used only to advertise/find direct host endpoints.
    bool HostOnline(const char *listenPort);
    bool JoinOnline(const char *hostAddress, const char *roomCode);
    bool JoinOnlineMatchmaking(const char *relayAddress);
    const char *GetMatchmakingRelayAddress();
    const char *GetOnlinePublicAddress();
    unsigned short GetOnlineListenPort();
    const char *GetOnlineRoomCode();
    bool IsOnline();

    // Bounded media transport (independent of controller polling/packets).
    // Frames are delivered as opaque payloads; producers should keep chunks
    // small and call these APIs outside latency-critical input handling.
    static const size_t kMaxMediaPayload = 1024;
    enum MediaType { MEDIA_AUDIO = 1, MEDIA_VIDEO = 2 };
    bool SendMediaFrame(unsigned char type, const void *payload, size_t size);
    bool ReceiveMediaFrame(unsigned char *type, unsigned short *sequence,
                           unsigned short *sourceNode, void *payload,
                           size_t capacity, size_t *size);

    void Stop();

    State GetState();
    const char *GetStatus();

    // Called for each emulator controller poll. Network packets carry only
    // controller state; emulation and ROM data stay local to every console.
    void Update(const OSContPad localPad[4], OSContPad outputPads[4]);
}

#endif
