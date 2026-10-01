#include "stdafx.h"
#include "SysCTR/Input/CTRMultiplayer.h"
#include "SysCTR/Input/CTRInput.h"

#include <3ds.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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

void Stop()
{
    if (s_state == STATE_HOSTING)
        udsDestroyNetwork();
    else if (s_state == STATE_JOINED)
        udsDisconnectNetwork();
    s_state = STATE_OFF;
    ResetRoomList();
    ClearInputs();
    SetStatus("Nearby multiplayer is off");
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
