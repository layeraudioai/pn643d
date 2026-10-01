#include "stdafx.h"
#include "SysCTR/Input/CTRMultiplayer.h"
#include "SysCTR/Input/CTRInput.h"

#include <3ds.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

namespace CTRMultiplayer
{
namespace
{
    // Keep these identifiers stable between builds so compatible Daedalus
    // installations can see one another in the UDS beacon scan.
    static const u32 kWlanCommId = 0x504E643D; // "PNd="
    static const u8 kNetworkId = 0x01;
    static const u8 kDataChannel = 1;
    static const u32 kSharedMemorySize = 0x3000;
    static const u32 kPacketMagic = 0x504E3634; // "PN64"
    static const u8 kProtocolVersion = 1;
    static const size_t kMaxRooms = 8;
    static const u64 kInputTimeoutMs = 500;
    static const u32 kOnlineSocBufferSize = 0x100000;
    static const u8 kOnlinePacketVersion = 1;
    static const size_t kOnlineHelloSize = 14;
    static const size_t kOnlineWelcomeSize = 16;
    static const size_t kOnlineInputSize = 12;
    static const size_t kOnlineStateSize = 25;

    enum PacketType { PACKET_INPUT = 1, PACKET_STATE = 2 };

    // Explicit byte-sized fields keep the wire format independent of the
    // compiler's OSContPad and struct padding.
    struct WirePad
    {
        u16 buttons;
        s8 stickX;
        s8 stickY;
    };

    struct Packet
    {
        u32 magic;
        u8 version;
        u8 type;
        u16 sequence;
        u8 playerSlot;
        u8 nodeSlots[UDS_MAXNODES];
        WirePad pads[4];
    };

    struct PeerInput
    {
        bool active;
        u16 sequence;
        u64 lastUpdate;
        OSContPad pad;
    };

    static State s_state = STATE_OFF;
    static bool s_udsInitialized = false;
    static udsBindContext s_bindContext;
    static udsNetworkScanInfo *s_rooms = NULL;
    static size_t s_roomCount = 0;
    static u8 s_scanBuffer[0x10000] __attribute__((aligned(4)));
    static char s_status[64] = "Nearby multiplayer is off";
    static PeerInput s_peers[UDS_MAXNODES];
    static u8 s_nodeSlots[UDS_MAXNODES];
    static OSContPad s_hostPads[4];
    static u16 s_txSequence = 0;
    static u16 s_rxSequence = 0;
    static bool s_haveRxSequence = false;

    static int s_onlineSocket = -1;
    static bool s_socInitialized = false;
    static u8 s_socBuffer[kOnlineSocBufferSize] __attribute__((aligned(0x1000)));
    static u8 s_onlineTx[kOnlineInputSize];
    static size_t s_onlineTxSize = 0;
    static size_t s_onlineTxOffset = 0;
    static u8 s_onlineRx[kOnlineStateSize];
    static size_t s_onlineRxSize = 0;
    static u8 s_onlineSlot = 0xFF;
    static u16 s_onlineTxSequence = 0;
    static u16 s_onlineRxSequence = 0;
    static bool s_onlineHaveRxSequence = false;
    static u8 s_onlineActiveMask = 0;
    static char s_onlineRoomCode[9] = "";

    static void SetStatus(const char *text)
    {
        snprintf(s_status, sizeof(s_status), "%s", text ? text : "");
    }

    static bool EnsureUds()
    {
        if (s_udsInitialized)
            return true;
        Result rc = udsInit(kSharedMemorySize, "Daedalus");
        if (R_FAILED(rc))
        {
            SetStatus("Wireless service unavailable");
            return false;
        }
        s_udsInitialized = true;
        return true;
    }

    static void ResetRoomList()
    {
        free(s_rooms);
        s_rooms = NULL;
        s_roomCount = 0;
    }

    static void ClearInputs()
    {
        memset(s_peers, 0, sizeof(s_peers));
        memset(s_nodeSlots, 0xFF, sizeof(s_nodeSlots));
        memset(s_hostPads, 0, sizeof(s_hostPads));
        s_txSequence = 0;
        s_rxSequence = 0;
        s_haveRxSequence = false;
    }

    static void EncodePads(Packet &packet, const OSContPad *pads)
    {
        for (unsigned i = 0; i < 4; ++i)
        {
            packet.pads[i].buttons = (u16)pads[i].button;
            packet.pads[i].stickX = (s8)pads[i].stick_x;
            packet.pads[i].stickY = (s8)pads[i].stick_y;
        }
    }

    static void DecodePads(const Packet &packet, OSContPad *pads)
    {
        for (unsigned i = 0; i < 4; ++i)
        {
            pads[i].button = packet.pads[i].buttons;
            pads[i].stick_x = packet.pads[i].stickX;
            pads[i].stick_y = packet.pads[i].stickY;
        }
    }

    static int AllocateSlot(u16 nodeId)
    {
        if (nodeId == UDS_HOST_NETWORKNODEID)
            return 0;
        if (nodeId >= UDS_MAXNODES)
            return -1;
        if (s_nodeSlots[nodeId] < 4)
            return s_nodeSlots[nodeId];

        bool used[4] = { false, false, false, false };
        for (unsigned node = 1; node < UDS_MAXNODES; ++node)
            if (s_nodeSlots[node] < 4)
                used[s_nodeSlots[node]] = true;
        for (unsigned slot = 0; slot < 4; ++slot)
        {
            if (!used[slot])
            {
                s_nodeSlots[nodeId] = (u8)slot;
                return (int)slot;
            }
        }
        return -1;
    }

    static bool ReadPackets()
    {
        bool gotAny = false;
        for (unsigned i = 0; i < 16; ++i)
        {
            if (!udsWaitDataAvailable(&s_bindContext, false, false))
                break;

            Packet packet;
            size_t actualSize = 0;
            u16 source = 0;
            Result rc = udsPullPacket(&s_bindContext, &packet, sizeof(packet), &actualSize, &source);
            if (R_FAILED(rc) || actualSize != sizeof(packet) ||
                packet.magic != kPacketMagic || packet.version != kProtocolVersion)
                continue;

            if (s_state == STATE_HOSTING && packet.type == PACKET_INPUT)
            {
                int slot = AllocateSlot(source);
                if (slot < 0 || slot == 0)
                    continue;
                PeerInput &peer = s_peers[source];
                // Ignore delayed/reordered input packets using wrap-safe sequence
                // comparison. First packet initializes the sequence window.
                if (!peer.active || (s16)(packet.sequence - peer.sequence) > 0)
                {
                    peer.active = true;
                    peer.sequence = packet.sequence;
                    peer.lastUpdate = osGetTime();
                    peer.pad.button = packet.pads[0].buttons;
                    peer.pad.stick_x = packet.pads[0].stickX;
                    peer.pad.stick_y = packet.pads[0].stickY;
                }
                gotAny = true;
            }
            else if (s_state == STATE_JOINED && packet.type == PACKET_STATE)
            {
                if (!s_haveRxSequence || (s16)(packet.sequence - s_rxSequence) > 0)
                {
                    s_haveRxSequence = true;
                    s_rxSequence = packet.sequence;
                    DecodePads(packet, s_hostPads);
                    memcpy(s_nodeSlots, packet.nodeSlots, sizeof(s_nodeSlots));
                }
                gotAny = true;
            }
        }
        return gotAny;
    }

    static void Put16(u8 *dst, u16 value)
    {
        dst[0] = (u8)(value >> 8);
        dst[1] = (u8)value;
    }

    static u16 Get16(const u8 *src)
    {
        return (u16)(((u16)src[0] << 8) | src[1]);
    }

    static bool WaitSocket(int fd, bool writing, int timeoutSeconds)
    {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        struct timeval timeout;
        timeout.tv_sec = timeoutSeconds;
        timeout.tv_usec = 0;
        const int result = select(fd + 1, writing ? NULL : &set,
                                  writing ? &set : NULL, NULL, &timeout);
        return result > 0;
    }

    static bool SendBlocking(int fd, const u8 *data, size_t size)
    {
        size_t sent = 0;
        while (sent < size)
        {
            if (!WaitSocket(fd, true, 8))
                return false;
            const ssize_t count = send(fd, data + sent, size - sent, 0);
            if (count > 0)
                sent += (size_t)count;
            else if (count < 0 && errno == EINTR)
                continue;
            else
                return false;
        }
        return true;
    }

    static bool ReceiveBlocking(int fd, u8 *data, size_t size)
    {
        size_t received = 0;
        while (received < size)
        {
            if (!WaitSocket(fd, false, 8))
                return false;
            const ssize_t count = recv(fd, data + received, size - received, 0);
            if (count > 0)
                received += (size_t)count;
            else if (count < 0 && errno == EINTR)
                continue;
            else
                return false;
        }
        return true;
    }

    static void CloseOnlineSocket()
    {
        if (s_onlineSocket >= 0)
        {
            close(s_onlineSocket);
            s_onlineSocket = -1;
        }
        if (s_socInitialized)
        {
            socExit();
            s_socInitialized = false;
        }
        s_onlineTxSize = s_onlineTxOffset = s_onlineRxSize = 0;
        s_onlineSlot = 0xFF;
        s_onlineActiveMask = 0;
        s_onlineHaveRxSequence = false;
        s_onlineRoomCode[0] = '\0';
    }

    static bool ConnectOnline(const char *serverAddress, bool host, const char *roomCode)
    {
        if (!serverAddress || !serverAddress[0])
        {
            SetStatus("Enter relay address (host:port)");
            return false;
        }
        if (!s_socInitialized)
        {
            Result result = socInit((u32*)(uintptr_t)s_socBuffer, kOnlineSocBufferSize);
            if (R_FAILED(result))
            {
                SetStatus("Internet socket service unavailable");
                return false;
            }
            s_socInitialized = true;
        }

        char hostName[256];
        char service[8] = "37777";
        const char *colon = strrchr(serverAddress, ':');
        size_t hostLength = colon ? (size_t)(colon - serverAddress) : strlen(serverAddress);
        if (hostLength == 0 || hostLength >= sizeof(hostName))
        {
            SetStatus("Invalid relay address");
            CloseOnlineSocket();
            return false;
        }
        memcpy(hostName, serverAddress, hostLength);
        hostName[hostLength] = '\0';
        if (colon)
        {
            char *end = NULL;
            long port = strtol(colon + 1, &end, 10);
            if (!end || *end || port < 1 || port > 65535)
            {
                SetStatus("Invalid relay port");
                CloseOnlineSocket();
                return false;
            }
            snprintf(service, sizeof(service), "%ld", port);
        }

        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo *addresses = NULL;
        if (getaddrinfo(hostName, service, &hints, &addresses) != 0 || !addresses)
        {
            SetStatus("Could not resolve relay address");
            CloseOnlineSocket();
            return false;
        }

        int fd = -1;
        for (struct addrinfo *address = addresses; address; address = address->ai_next)
        {
            fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
            if (fd < 0)
                continue;
            const int oldFlags = fcntl(fd, F_GETFL, 0);
            if (oldFlags < 0 || fcntl(fd, F_SETFL, oldFlags | O_NONBLOCK) < 0)
            {
                close(fd);
                fd = -1;
                continue;
            }
            int rc = connect(fd, address->ai_addr, address->ai_addrlen);
            if (rc < 0 && errno == EINPROGRESS && WaitSocket(fd, true, 8))
            {
                int socketError = 0;
                socklen_t errorLength = sizeof(socketError);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &errorLength) == 0 && socketError == 0)
                    rc = 0;
            }
            if (rc == 0)
                break;
            close(fd);
            fd = -1;
        }
        freeaddrinfo(addresses);
        if (fd < 0)
        {
            SetStatus("Could not connect to relay");
            CloseOnlineSocket();
            return false;
        }

        int noDelay = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));
        u8 hello[kOnlineHelloSize];
        memcpy(hello, "PN64", 4);
        hello[4] = kOnlinePacketVersion;
        hello[5] = host ? 1 : 2;
        memset(hello + 6, ' ', 8);
        if (!host && roomCode)
        {
            size_t length = strlen(roomCode);
            if (length != 6)
            {
                close(fd);
                SetStatus("Room code must be 6 characters");
                CloseOnlineSocket();
                return false;
            }
            for (size_t i = 0; i < 6; ++i)
            {
                char c = roomCode[i];
                hello[6 + i] = (u8)(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
            }
        }
        u8 welcome[kOnlineWelcomeSize];
        if (!SendBlocking(fd, hello, sizeof(hello)) || !ReceiveBlocking(fd, welcome, sizeof(welcome)) ||
            memcmp(welcome, "PN64", 4) != 0 || welcome[4] != kOnlinePacketVersion || welcome[5] != 3)
        {
            close(fd);
            SetStatus("Relay rejected connection or timed out");
            CloseOnlineSocket();
            return false;
        }
        s_onlineSocket = fd;
        s_onlineSlot = welcome[6];
        memcpy(s_onlineRoomCode, welcome + 8, 8);
        s_onlineRoomCode[8] = '\0';
        for (int i = 7; i >= 0 && s_onlineRoomCode[i] == ' '; --i)
            s_onlineRoomCode[i] = '\0';
        s_onlineTxSequence = 0;
        s_onlineTxSize = s_onlineTxOffset = s_onlineRxSize = 0;
        s_onlineActiveMask = 0;
        s_onlineHaveRxSequence = false;
        return true;
    }

    static bool PollOnline(const OSContPad localPad[4], OSContPad outputPads[4])
    {
        if (s_onlineSocket < 0 || s_onlineSlot >= 4)
            return false;

        if (s_onlineTxSize == 0)
        {
            memcpy(s_onlineTx, "PN64", 4);
            s_onlineTx[4] = kOnlinePacketVersion;
            s_onlineTx[5] = 10;
            Put16(s_onlineTx + 6, ++s_onlineTxSequence);
            const OSContPad &pad = localPad[CTRInput_GetLocalControllerPort()];
            Put16(s_onlineTx + 8, (u16)pad.button);
            s_onlineTx[10] = (u8)(s8)pad.stick_x;
            s_onlineTx[11] = (u8)(s8)pad.stick_y;
            s_onlineTxSize = sizeof(s_onlineTx);
            s_onlineTxOffset = 0;
        }
        while (s_onlineTxOffset < s_onlineTxSize)
        {
            const ssize_t count = send(s_onlineSocket, s_onlineTx + s_onlineTxOffset,
                                       s_onlineTxSize - s_onlineTxOffset, 0);
            if (count > 0)
                s_onlineTxOffset += (size_t)count;
            else if (count < 0 && (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR))
                break;
            else
                return false;
        }
        if (s_onlineTxOffset == s_onlineTxSize)
            s_onlineTxSize = s_onlineTxOffset = 0;

        for (;;)
        {
            const ssize_t count = recv(s_onlineSocket, s_onlineRx + s_onlineRxSize,
                                       sizeof(s_onlineRx) - s_onlineRxSize, 0);
            if (count > 0)
            {
                s_onlineRxSize += (size_t)count;
                if (s_onlineRxSize == sizeof(s_onlineRx))
                {
                    if (memcmp(s_onlineRx, "PN64", 4) == 0 &&
                        s_onlineRx[4] == kOnlinePacketVersion && s_onlineRx[5] == 11)
                    {
                        const u16 sequence = Get16(s_onlineRx + 6);
                        if (!s_onlineHaveRxSequence || (s16)(sequence - s_onlineRxSequence) > 0)
                        {
                            s_onlineHaveRxSequence = true;
                            s_onlineRxSequence = sequence;
                            s_onlineActiveMask = s_onlineRx[8];
                            for (unsigned i = 0; i < 4; ++i)
                            {
                                const u8 *wire = s_onlineRx + 9 + i * 4;
                                s_hostPads[i].button = Get16(wire);
                                s_hostPads[i].stick_x = (s8)wire[2];
                                s_hostPads[i].stick_y = (s8)wire[3];
                            }
                        }
                    }
                    s_onlineRxSize = 0;
                }
            }
            else if (count == 0)
                return false;
            else if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
                break;
            else
                return false;
        }

        memcpy(outputPads, s_hostPads, sizeof(s_hostPads));
        const OSContPad &local = localPad[CTRInput_GetLocalControllerPort()];
        outputPads[s_onlineSlot] = local;
        return true;
    }
}

bool Host()
{
    Stop();
    if (!EnsureUds())
        return false;

    udsNetworkStruct network;
    udsGenerateDefaultNetworkStruct(&network, kWlanCommId, kNetworkId, 4);
    memset(&s_bindContext, 0, sizeof(s_bindContext));
    ClearInputs();
    Result rc = udsCreateNetwork(&network, NULL, 0, &s_bindContext,
                                 kDataChannel, UDS_DEFAULT_RECVBUFSIZE);
    if (R_FAILED(rc))
    {
        SetStatus("Could not start nearby room");
        return false;
    }

    s_nodeSlots[UDS_HOST_NETWORKNODEID] = (u8)CTRInput_GetLocalControllerPort();
    s_state = STATE_HOSTING;
    SetStatus("Hosting nearby room - waiting for players");
    return true;
}

bool Scan()
{
    if (s_state != STATE_OFF)
    {
        SetStatus("Stop the active session before scanning");
        return false;
    }
    ResetRoomList();
    if (!EnsureUds())
        return false;

    size_t count = 0;
    Result rc = udsScanBeacons(s_scanBuffer, sizeof(s_scanBuffer), &s_rooms,
                               &count, kWlanCommId, kNetworkId, NULL, false);
    if (R_FAILED(rc))
    {
        ResetRoomList();
        SetStatus("Nearby room scan failed");
        return false;
    }
    s_roomCount = count < kMaxRooms ? count : kMaxRooms;
    if (s_roomCount == 0)
        SetStatus("No nearby rooms found - scan again");
    else
        SetStatus("Select a nearby room to join");
    return true;
}

size_t GetRoomCount()
{
    return s_roomCount;
}

void GetRoomLabel(size_t roomIndex, char *buffer, size_t bufferSize)
{
    if (!buffer || bufferSize == 0)
        return;
    if (!s_rooms || roomIndex >= s_roomCount)
    {
        snprintf(buffer, bufferSize, "Room");
        return;
    }

    char username[32] = "Host";
    if (R_FAILED(udsGetNodeInfoUsername(&s_rooms[roomIndex].nodes[0], username)))
        snprintf(username, sizeof(username), "Host");
    username[sizeof(username) - 1] = '\0';
    snprintf(buffer, bufferSize, "%s (%u players)", username,
             (unsigned)s_rooms[roomIndex].network.total_nodes);
}

bool Join(size_t roomIndex)
{
    if (s_state != STATE_OFF || !s_rooms || roomIndex >= s_roomCount)
        return false;
    if (!EnsureUds())
        return false;

    ClearInputs();
    memset(&s_bindContext, 0, sizeof(s_bindContext));
    Result rc = udsConnectNetwork(&s_rooms[roomIndex].network, NULL, 0,
                                  &s_bindContext, UDS_HOST_NETWORKNODEID,
                                  UDSCONTYPE_Client, kDataChannel,
                                  UDS_DEFAULT_RECVBUFSIZE);
    if (R_FAILED(rc))
    {
        SetStatus("Could not join room - try scanning again");
        return false;
    }
    s_state = STATE_JOINED;
    SetStatus("Joined nearby room - syncing controllers");
    return true;
}

bool HostOnline(const char *serverAddress)
{
    if (s_state != STATE_OFF)
    {
        SetStatus("Stop the active session first");
        return false;
    }
    ClearInputs();
    if (!ConnectOnline(serverAddress, true, NULL))
        return false;
    s_state = STATE_ONLINE_HOSTING;
    snprintf(s_status, sizeof(s_status), "Room %s - share this code", s_onlineRoomCode);
    return true;
}

bool JoinOnline(const char *serverAddress, const char *roomCode)
{
    if (s_state != STATE_OFF)
    {
        SetStatus("Stop the active session first");
        return false;
    }
    ClearInputs();
    if (!ConnectOnline(serverAddress, false, roomCode))
        return false;
    s_state = STATE_ONLINE_JOINED;
    snprintf(s_status, sizeof(s_status), "Joined room %s", s_onlineRoomCode);
    return true;
}

const char *GetOnlineRoomCode()
{
    return s_onlineRoomCode;
}

bool IsOnline()
{
    return s_state == STATE_ONLINE_HOSTING || s_state == STATE_ONLINE_JOINED;
}

void Stop()
{
    if (s_state == STATE_HOSTING)
        udsDestroyNetwork();
    else if (s_state == STATE_JOINED)
        udsDisconnectNetwork();
    if (IsOnline())
        CloseOnlineSocket();
    s_state = STATE_OFF;
    ResetRoomList();
    ClearInputs();
    SetStatus("Multiplayer is off");
}

State GetState()
{
    return s_state;
}

const char *GetStatus()
{
    return s_status;
}

void Update(const OSContPad localPad[4], OSContPad outputPads[4])
{
    memcpy(outputPads, localPad, sizeof(OSContPad) * 4);
    if (s_state == STATE_OFF)
        return;

    if (IsOnline())
    {
        if (!PollOnline(localPad, outputPads))
        {
            CloseOnlineSocket();
            s_state = STATE_OFF;
            ClearInputs();
            SetStatus("Relay connection lost - session stopped");
        }
        return;
    }

    ReadPackets();
    if (s_state == STATE_HOSTING)
    {
        memcpy(s_hostPads, localPad, sizeof(s_hostPads));
        const u64 now = osGetTime();
        for (unsigned node = 2; node < UDS_MAXNODES; ++node)
        {
            if (s_peers[node].active && now - s_peers[node].lastUpdate > kInputTimeoutMs)
            {
                s_peers[node].active = false;
                s_nodeSlots[node] = 0xFF;
                memset(&s_peers[node].pad, 0, sizeof(OSContPad));
            }
            if (s_peers[node].active && s_nodeSlots[node] < 4)
                s_hostPads[s_nodeSlots[node]] = s_peers[node].pad;
        }

        Packet packet;
        memset(&packet, 0, sizeof(packet));
        packet.magic = kPacketMagic;
        packet.version = kProtocolVersion;
        packet.type = PACKET_STATE;
        packet.sequence = ++s_txSequence;
        memcpy(packet.nodeSlots, s_nodeSlots, sizeof(s_nodeSlots));
        EncodePads(packet, s_hostPads);
        udsSendTo(UDS_BROADCAST_NETWORKNODEID, kDataChannel,
                  UDS_SENDFLAG_Default, &packet, sizeof(packet));
        memcpy(outputPads, s_hostPads, sizeof(s_hostPads));
    }
    else
    {
        udsConnectionStatus connection;
        memset(&connection, 0, sizeof(connection));
        if (R_SUCCEEDED(udsGetConnectionStatus(&connection)) &&
            connection.cur_NetworkNodeID < UDS_MAXNODES)
        {
            Packet packet;
            memset(&packet, 0, sizeof(packet));
            packet.magic = kPacketMagic;
            packet.version = kProtocolVersion;
            packet.type = PACKET_INPUT;
            packet.sequence = ++s_txSequence;
            packet.playerSlot = 0;
            const OSContPad &ownPad = localPad[CTRInput_GetLocalControllerPort()];
            packet.pads[0].buttons = (u16)ownPad.button;
            packet.pads[0].stickX = (s8)ownPad.stick_x;
            packet.pads[0].stickY = (s8)ownPad.stick_y;
            udsSendTo(UDS_HOST_NETWORKNODEID, kDataChannel,
                      UDS_SENDFLAG_Default, &packet, sizeof(packet));

            const u8 ownSlot = s_nodeSlots[connection.cur_NetworkNodeID];
            if (ownSlot < 4)
            {
                memcpy(outputPads, s_hostPads, sizeof(s_hostPads));
                outputPads[ownSlot] = localPad[0];
            }
        }
    }
}
}
