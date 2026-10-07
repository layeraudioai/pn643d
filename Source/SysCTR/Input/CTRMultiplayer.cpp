#include "stdafx.h"
#include "SysCTR/Input/CTRMultiplayer.h"
#include "SysCTR/Input/CTRMultiplayerConfig.h"
#include "SysCTR/Input/CTRInput.h"
#include "SysCTR/Input/CTRMediaFrame.h"
#include "Core/ROM.h"

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
    // Keep media on its own UDS data channel so audio/video traffic cannot
    // consume controller packet receive queues or delay input snapshots.
    static const u8 kDataChannel = 1;
    static const u8 kMediaChannel = 2;
    static const u32 kSharedMemorySize = 0x3000;
    static const u32 kPacketMagic = 0x504E3634; // "PN64"
    // Version 2 adds the active ROM identity to local UDS packets so consoles
    // cannot accidentally combine inputs from different games/regions.
    static const u8 kProtocolVersion = 2;
    static const size_t kMaxRooms = 8;
    static const u64 kInputTimeoutMs = 500;
    static const u32 kOnlineSocBufferSize = 0x100000;
    static const u8 kOnlinePacketVersion = 3;
    static const size_t kOnlineHelloSize = 23;
    static const u8 kRegistryVersion = 1;
    static const size_t kRegistryHelloSize = 25;
    static const size_t kRegistryAckSize = 22;
    static const size_t kRegistryMatchSize = 20;
    static const u16 kDefaultOnlinePort = 37777;
    static const u64 kRegistryHeartbeatMs = 20000;
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
        u32 gameCrc1;
        u32 gameCrc2;
        u8 gameCountry;
    };

    struct PeerInput
    {
        bool active;
        u16 sequence;
        u64 lastUpdate;
        OSContPad pad;
    };

    struct QueuedMediaFrame
    {
        u8 type;
        u16 sequence;
        u16 source;
        size_t size;
        u8 payload[CTRMediaFrame::kMaxPayload];
    };
    static const unsigned kMediaQueueCapacity = 8;
    static QueuedMediaFrame s_mediaQueue[kMediaQueueCapacity];
    static unsigned s_mediaRead = 0;
    static unsigned s_mediaWrite = 0;
    static unsigned s_mediaCount = 0;
    static u16 s_mediaSequence = 0;

    static State s_state = STATE_OFF;
    static bool s_udsInitialized = false;
    static udsBindContext s_bindContext;
    static udsBindContext s_mediaBindContext;
    static bool s_mediaBound = false;
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
    static bool s_stateSyncPending = false;
    static u64 s_lastStateSync = 0;
    // Full controller snapshots go out at 10 Hz (and immediately after a
    // slot is allocated), comfortably exceeding the once-per-second minimum.
    static const u64 kStateSyncIntervalMs = 100;

    static int s_onlineSocket = -1;
    static bool s_socInitialized = false;
    static u32 s_socBuffer[kOnlineSocBufferSize / sizeof(u32)] __attribute__((aligned(0x1000)));
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
    static char s_localRoomCode[7] = "";
    static const char *kRoomAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    static const u8 kLocalAppDataSize = 19;
    static u16 s_onlineListenPort = kDefaultOnlinePort;
    static int s_registrySocket = -1;
    static u64 s_lastRegistryHeartbeat = 0;
    static int s_registrySearchSocket = -1;
    static u8 s_registrySearchRx[kRegistryMatchSize];
    static size_t s_registrySearchRxSize = 0;
    static char s_onlinePublicAddress[64] = "";

    // The 3DS hosts the direct gameplay socket. The optional PC service only
    // advertises this endpoint for matchmaking; controller packets bypass it.
    struct OnlinePeer
    {
        int socket;
        bool handshaking;
        u8 hello[kOnlineHelloSize];
        size_t helloSize;
        u8 input[kOnlineInputSize];
        size_t inputSize;
        u8 output[kOnlineWelcomeSize + kOnlineStateSize];
        size_t outputSize;
        size_t outputOffset;
        u8 slot;
        u64 lastActivity;
        u16 lastSequence;
        bool haveSequence;
    };

    static int s_onlineListener = -1;
    static OnlinePeer s_onlinePeers[3] = { { -1 }, { -1 }, { -1 } };
    static OSContPad s_serverPads[4];
    static u8 s_serverActiveMask = 0;
    static u16 s_serverSequence = 0;
    static const u64 kServerPeerTimeoutMs = 60000;

    static void GenerateRoomCode(char code[7])
    {
        u64 seed = osGetTime() ^ ((u64)g_ROM.mRomID.CRC[0] << 17) ^
                   ((u64)g_ROM.mRomID.CRC[1] << 3);
        for (unsigned i = 0; i < 6; ++i)
        {
            seed = seed * 1103515245u + 12345u;
            code[i] = kRoomAlphabet[(seed >> 16) % 32];
        }
        code[6] = '\0';
    }

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

    static void UnbindMedia()
    {
        if (s_mediaBound)
        {
            udsUnbind(&s_mediaBindContext);
            s_mediaBound = false;
            memset(&s_mediaBindContext, 0, sizeof(s_mediaBindContext));
        }
        s_mediaRead = s_mediaWrite = s_mediaCount = 0;
        s_mediaSequence = 0;
    }

    static bool BindMedia(u16 nodeId)
    {
        memset(&s_mediaBindContext, 0, sizeof(s_mediaBindContext));
        Result rc = udsBind(&s_mediaBindContext, nodeId, false, kMediaChannel,
                            UDS_DEFAULT_RECVBUFSIZE);
        if (R_FAILED(rc))
        {
            SetStatus("Controller session active; media channel unavailable");
            return false;
        }
        s_mediaBound = true;
        return true;
    }

    static void PumpUdsMedia()
    {
        if (!s_mediaBound)
            return;
        // Strict per-frame and per-poll bounds prevent media bursts from
        // monopolizing the emulator's controller update path.
        for (unsigned n = 0; n < 4 && s_mediaCount < kMediaQueueCapacity; ++n)
        {
            if (!udsWaitDataAvailable(&s_mediaBindContext, false, false))
                break;
            u8 wire[CTRMediaFrame::kMaxWireSize];
            size_t actualSize = 0;
            u16 source = 0;
            Result rc = udsPullPacket(&s_mediaBindContext, wire, sizeof(wire),
                                      &actualSize, &source);
            if (R_FAILED(rc))
                break;
            QueuedMediaFrame &frame = s_mediaQueue[s_mediaWrite];
            const u8 *payload = NULL;
            if (!CTRMediaFrame::Decode(wire, actualSize, &frame.type,
                                       &frame.sequence, &payload, &frame.size))
                continue;
            frame.source = source;
            if (frame.size)
                memcpy(frame.payload, payload, frame.size);
            s_mediaWrite = (s_mediaWrite + 1) % kMediaQueueCapacity;
            ++s_mediaCount;
        }
    }

    static void ClearInputs()
    {
        memset(s_peers, 0, sizeof(s_peers));
        memset(s_nodeSlots, 0xFF, sizeof(s_nodeSlots));
        memset(s_hostPads, 0, sizeof(s_hostPads));
        s_txSequence = 0;
        s_rxSequence = 0;
        s_haveRxSequence = false;
        s_stateSyncPending = false;
        s_lastStateSync = 0;
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

    static void SetPacketGameIdentity(Packet &packet)
    {
        packet.gameCrc1 = g_ROM.mRomID.CRC[0];
        packet.gameCrc2 = g_ROM.mRomID.CRC[1];
        packet.gameCountry = g_ROM.mRomID.CountryID;
    }

    static bool PacketMatchesCurrentGame(const Packet &packet)
    {
        return packet.gameCrc1 == g_ROM.mRomID.CRC[0] &&
               packet.gameCrc2 == g_ROM.mRomID.CRC[1] &&
               packet.gameCountry == g_ROM.mRomID.CountryID;
    }

    static void EncodeGameIdentity(u8 *dst)
    {
        const u32 crc1 = g_ROM.mRomID.CRC[0];
        const u32 crc2 = g_ROM.mRomID.CRC[1];
        dst[0] = (u8)(crc1 >> 24); dst[1] = (u8)(crc1 >> 16);
        dst[2] = (u8)(crc1 >> 8);  dst[3] = (u8)crc1;
        dst[4] = (u8)(crc2 >> 24); dst[5] = (u8)(crc2 >> 16);
        dst[6] = (u8)(crc2 >> 8);  dst[7] = (u8)crc2;
        dst[8] = g_ROM.mRomID.CountryID;
    }

    static bool LocalRoomMatchesGame(const udsNetworkStruct &network)
    {
        u8 identity[9];
        EncodeGameIdentity(identity);
        return network.appdata_size >= kLocalAppDataSize &&
               memcmp(network.appdata, "PNL1", 4) == 0 &&
               memcmp(network.appdata + 10, identity, sizeof(identity)) == 0;
    }

    static bool LocalRoomAvailable(const udsNetworkStruct &network)
    {
        return LocalRoomMatchesGame(network) && network.total_nodes < network.max_nodes;
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
        // Reserve every port assigned to the host before allocating remote
        // players. In GoldenEye dual-controller mode this reserves both P1/P2.
        const unsigned hostMask = CTRInput_GetLocalControllerPortMask();
        for (unsigned slot = 0; slot < 4; ++slot)
            if (hostMask & (1u << slot))
                used[slot] = true;
        for (unsigned node = 1; node < UDS_MAXNODES; ++node)
            if (node != UDS_HOST_NETWORKNODEID && s_nodeSlots[node] < 4)
                used[s_nodeSlots[node]] = true;
        for (unsigned slot = 0; slot < 4; ++slot)
        {
            if (!used[slot])
            {
                s_nodeSlots[nodeId] = (u8)slot;
                s_stateSyncPending = true;
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
                // A room is only meaningful when every console is running the
                // same ROM revision and region. Ignore input from mismatches;
                // the authoritative state packet below tells that client why.
                if (!PacketMatchesCurrentGame(packet))
                {
                    SetStatus("Player rejected: different game or region");
                    continue;
                }
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
                if (!PacketMatchesCurrentGame(packet))
                {
                    udsDisconnectNetwork();
                    s_state = STATE_OFF;
                    ClearInputs();
                    SetStatus("Game mismatch: load the host's same ROM/region");
                    return true;
                }
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

    static bool RegisterMatchmakingRoom(const char *relayAddress);
    static bool BeginMatchmakingSearch(const char *relayAddress);

    static bool SetNonBlocking(int fd)
    {
        const int flags = fcntl(fd, F_GETFL, 0);
        return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
    }

    static void ResetServerPeer(OnlinePeer &peer)
    {
        if (peer.socket >= 0)
            close(peer.socket);
        if (peer.slot < 4)
        {
            s_serverActiveMask &= (u8)~(1u << peer.slot);
            memset(&s_serverPads[peer.slot], 0, sizeof(OSContPad));
        }
        memset(&peer, 0, sizeof(peer));
        peer.socket = -1;
        peer.slot = 0xFF;
    }

    static void QueueServerState(OnlinePeer &peer)
    {
        if (peer.outputOffset < peer.outputSize)
            return;
        u8 *state = peer.output;
        memcpy(state, "PN64", 4);
        state[4] = kOnlinePacketVersion;
        state[5] = 11;
        Put16(state + 6, ++s_serverSequence);
        state[8] = s_serverActiveMask;
        for (unsigned i = 0; i < 4; ++i)
        {
            u8 *wire = state + 9 + i * 4;
            Put16(wire, (u16)s_serverPads[i].button);
            wire[2] = (u8)(s8)s_serverPads[i].stick_x;
            wire[3] = (u8)(s8)s_serverPads[i].stick_y;
        }
        peer.outputSize = kOnlineStateSize;
        peer.outputOffset = 0;
    }

    static bool RoomCodeMatches(const u8 *code)
    {
        for (unsigned i = 0; i < 6; ++i)
        {
            char c = (char)code[i];
            if (c >= 'a' && c <= 'z')
                c = (char)(c - 'a' + 'A');
            if (c != s_onlineRoomCode[i])
                return false;
        }
        return true;
    }

    static void AcceptOnlinePeers()
    {
        for (unsigned accepted = 0; accepted < 3; ++accepted)
        {
            struct sockaddr_in address;
            socklen_t addressSize = sizeof(address);
            const int fd = accept(s_onlineListener, (struct sockaddr *)&address, &addressSize);
            if (fd < 0)
            {
                if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
                    break;
                break;
            }
            if (!SetNonBlocking(fd))
            {
                close(fd);
                continue;
            }
            int noDelay = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));
            unsigned i;
            for (i = 0; i < 3 && s_onlinePeers[i].socket >= 0; ++i) {}
            if (i == 3)
            {
                close(fd);
                continue;
            }
            OnlinePeer &peer = s_onlinePeers[i];
            memset(&peer, 0, sizeof(peer));
            peer.socket = fd;
            peer.slot = 0xFF;
            peer.handshaking = true;
            peer.lastActivity = osGetTime();
        }
    }

    static void PollOnlineServer(const OSContPad localPad[4], OSContPad outputPads[4])
    {
        AcceptOnlinePeers();
        const u64 now = osGetTime();
        if (s_registrySocket >= 0 && now - s_lastRegistryHeartbeat >= kRegistryHeartbeatMs)
        {
            const u8 heartbeat = 0x48;
            const ssize_t sent = send(s_registrySocket, &heartbeat, 1, 0);
            if (sent == 1)
                s_lastRegistryHeartbeat = now;
            else if (sent < 0 && errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR)
            {
                close(s_registrySocket);
                s_registrySocket = -1;
                snprintf(s_status, sizeof(s_status), "Room %.6s direct; matchmaking relay lost",
                         s_onlineRoomCode);
            }
        }
        const unsigned hostMask = CTRInput_GetLocalControllerPortMask();
        for (unsigned slot = 0; slot < 4; ++slot)
        {
            if (hostMask & (1u << slot))
            {
                s_serverPads[slot] = localPad[slot];
                s_serverActiveMask |= (u8)(1u << slot);
            }
        }

        for (unsigned i = 0; i < 3; ++i)
        {
            OnlinePeer &peer = s_onlinePeers[i];
            if (peer.socket < 0)
                continue;

            if (peer.outputOffset < peer.outputSize)
            {
                const ssize_t sent = send(peer.socket, peer.output + peer.outputOffset,
                                          peer.outputSize - peer.outputOffset, 0);
                if (sent > 0)
                    peer.outputOffset += (size_t)sent;
                else if (sent < 0 && errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR)
                {
                    ResetServerPeer(peer);
                    continue;
                }
                if (peer.outputOffset == peer.outputSize)
                    peer.outputSize = peer.outputOffset = 0;
            }

            u8 *buffer = peer.handshaking ? peer.hello : peer.input;
            size_t *bufferSize = peer.handshaking ? &peer.helloSize : &peer.inputSize;
            const size_t expectedSize = peer.handshaking ? kOnlineHelloSize : kOnlineInputSize;
            const ssize_t received = recv(peer.socket, buffer + *bufferSize,
                                          expectedSize - *bufferSize, 0);
            if (received > 0)
            {
                *bufferSize += (size_t)received;
                peer.lastActivity = now;
            }
            else if (received == 0 || (received < 0 && errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR))
            {
                ResetServerPeer(peer);
                continue;
            }

            if (peer.handshaking && peer.helloSize == kOnlineHelloSize)
            {
                u8 gameIdentity[9];
                EncodeGameIdentity(gameIdentity);
                if (memcmp(peer.hello, "PN64", 4) != 0 || peer.hello[4] != kOnlinePacketVersion ||
                    peer.hello[5] != 2 || !RoomCodeMatches(peer.hello + 6) ||
                    memcmp(peer.hello + 14, gameIdentity, sizeof(gameIdentity)) != 0)
                {
                    ResetServerPeer(peer);
                    continue;
                }
                bool slotUsed[4] = { false, false, false, false };
                const unsigned hostMask = CTRInput_GetLocalControllerPortMask();
                for (unsigned slot = 0; slot < 4; ++slot)
                    if (hostMask & (1u << slot))
                        slotUsed[slot] = true;
                for (unsigned p = 0; p < 3; ++p)
                    if (s_onlinePeers[p].socket >= 0 && !s_onlinePeers[p].handshaking && s_onlinePeers[p].slot < 4)
                        slotUsed[s_onlinePeers[p].slot] = true;
                unsigned slot;
                for (slot = 0; slot < 4 && slotUsed[slot]; ++slot) {}
                if (slot == 4)
                {
                    ResetServerPeer(peer);
                    continue;
                }
                peer.slot = (u8)slot;
                peer.handshaking = false;
                s_serverActiveMask |= (u8)(1u << slot);
                u8 *welcome = peer.output;
                memcpy(welcome, "PN64", 4);
                welcome[4] = kOnlinePacketVersion;
                welcome[5] = 3;
                welcome[6] = (u8)slot;
                welcome[7] = 0;
                memset(welcome + 8, ' ', 8);
                memcpy(welcome + 8, s_onlineRoomCode, 6);
                peer.outputSize = kOnlineWelcomeSize;
                peer.outputOffset = 0;
                // Queue the first state immediately after the welcome is sent.
                peer.inputSize = 0;
            }
            else if (!peer.handshaking && peer.inputSize == kOnlineInputSize)
            {
                const u8 *packet = peer.input;
                if (memcmp(packet, "PN64", 4) == 0 && packet[4] == kOnlinePacketVersion && packet[5] == 10)
                {
                    const u16 sequence = Get16(packet + 6);
                    if (!peer.haveSequence || (s16)(sequence - peer.lastSequence) > 0)
                    {
                        peer.haveSequence = true;
                        peer.lastSequence = sequence;
                        s_serverPads[peer.slot].button = Get16(packet + 8);
                        s_serverPads[peer.slot].stick_x = (s8)packet[10];
                        s_serverPads[peer.slot].stick_y = (s8)packet[11];
                    }
                }
                else
                {
                    ResetServerPeer(peer);
                    continue;
                }
                peer.inputSize = 0;
            }

            if (peer.socket >= 0 && now - peer.lastActivity > kServerPeerTimeoutMs)
                ResetServerPeer(peer);
        }

        for (unsigned i = 0; i < 3; ++i)
        {
            OnlinePeer &peer = s_onlinePeers[i];
            if (peer.socket < 0 || peer.handshaking)
                continue;
            if (peer.outputSize == 0)
                QueueServerState(peer);
        }
        memcpy(outputPads, s_serverPads, sizeof(s_serverPads));
    }

    static bool StartOnlineHost(const char *portText)
    {
        long port = 37777;
        if (portText && portText[0])
        {
            char *end = NULL;
            port = strtol(portText, &end, 10);
            if (!end || *end || port < 1 || port > 65535)
            {
                SetStatus("Invalid TCP port (1-65535)");
                return false;
            }
        }
        if (!s_socInitialized)
        {
            Result result = socInit(s_socBuffer, sizeof(s_socBuffer));
            if (R_FAILED(result))
            {
                SetStatus("Internet socket service unavailable");
                return false;
            }
            s_socInitialized = true;
        }
        s_onlineListener = socket(AF_INET, SOCK_STREAM, 0);
        if (s_onlineListener < 0)
        {
            socExit();
            s_socInitialized = false;
            SetStatus("Could not open TCP listener");
            return false;
        }
        int reuse = 1;
        setsockopt(s_onlineListener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        struct sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons((u16)port);
        if (bind(s_onlineListener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
            listen(s_onlineListener, 3) < 0 || !SetNonBlocking(s_onlineListener))
        {
            close(s_onlineListener);
            s_onlineListener = -1;
            socExit();
            s_socInitialized = false;
            SetStatus("TCP port unavailable - check port and Wi-Fi");
            return false;
        }

        for (unsigned i = 0; i < 3; ++i)
        {
            memset(&s_onlinePeers[i], 0, sizeof(s_onlinePeers[i]));
            s_onlinePeers[i].socket = -1;
            s_onlinePeers[i].slot = 0xFF;
        }
        memset(s_serverPads, 0, sizeof(s_serverPads));
        s_serverActiveMask = 0;
        s_serverSequence = 0;
        char generatedCode[7];
        GenerateRoomCode(generatedCode);
        memcpy(s_onlineRoomCode, generatedCode, 6);
        s_onlineRoomCode[6] = s_onlineRoomCode[7] = ' ';
        s_onlineRoomCode[8] = '\0';
        s_onlineSlot = CTRInput_GetLocalControllerPort();
        s_onlineListenPort = (u16)port;
        s_onlineTxSize = s_onlineTxOffset = s_onlineRxSize = 0;
        s_state = STATE_ONLINE_HOSTING;
        snprintf(s_status, sizeof(s_status), "Room %.6s - port %ld", s_onlineRoomCode, port);
        return true;
    }

    static void CloseOnlineServer()
    {
        if (s_onlineListener >= 0)
        {
            close(s_onlineListener);
            s_onlineListener = -1;
        }
        for (unsigned i = 0; i < 3; ++i)
            ResetServerPeer(s_onlinePeers[i]);
        memset(s_serverPads, 0, sizeof(s_serverPads));
        s_serverActiveMask = 0;
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

    static int OpenTcpConnection(const char *serverAddress, u16 defaultPort)
    {
        if (!serverAddress || !serverAddress[0])
            return -1;
        if (!s_socInitialized)
        {
            if (R_FAILED(socInit(s_socBuffer, sizeof(s_socBuffer))))
                return -1;
            s_socInitialized = true;
        }

        char hostName[256];
        char service[8];
        snprintf(service, sizeof(service), "%u", (unsigned)defaultPort);
        const char *colon = strrchr(serverAddress, ':');
        const size_t hostLength = colon ? (size_t)(colon - serverAddress) : strlen(serverAddress);
        if (!hostLength || hostLength >= sizeof(hostName))
            return -1;
        memcpy(hostName, serverAddress, hostLength);
        hostName[hostLength] = '\0';
        if (colon)
        {
            char *end = NULL;
            const long port = strtol(colon + 1, &end, 10);
            if (!end || *end || port < 1 || port > 65535)
                return -1;
            snprintf(service, sizeof(service), "%ld", port);
        }

        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo *addresses = NULL;
        if (getaddrinfo(hostName, service, &hints, &addresses) != 0 || !addresses)
            return -1;

        int fd = -1;
        for (struct addrinfo *address = addresses; address; address = address->ai_next)
        {
            fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
            if (fd < 0)
                continue;
            const int flags = fcntl(fd, F_GETFL, 0);
            if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
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
        if (fd >= 0)
        {
            const int flags = fcntl(fd, F_GETFL, 0);
            if (flags >= 0)
                fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
            int noDelay = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));
        }
        return fd;
    }

    static bool RegisterMatchmakingRoom(const char *relayAddress)
    {
        const int fd = OpenTcpConnection(relayAddress, 5000);
        if (fd < 0)
            return false;
        u8 hello[kRegistryHelloSize];
        memset(hello, 0, sizeof(hello));
        memcpy(hello, "PN64", 4);
        hello[4] = kRegistryVersion;
        hello[5] = 1; // host registration
        memset(hello + 6, ' ', 8);
        memcpy(hello + 6, s_onlineRoomCode, 8);
        Put16(hello + 14, s_onlineListenPort);
        EncodeGameIdentity(hello + 16);
        u8 ack[kRegistryAckSize];
        if (!SendBlocking(fd, hello, sizeof(hello)) || !ReceiveBlocking(fd, ack, sizeof(ack)) ||
            memcmp(ack, "PN64", 4) != 0 || ack[4] != kRegistryVersion || ack[5] != 6)
        {
            close(fd);
            return false;
        }
        if (!SetNonBlocking(fd))
        {
            close(fd);
            return false;
        }
        if (s_registrySocket >= 0)
            close(s_registrySocket);
        s_registrySocket = fd;
        struct in_addr publicAddress;
        memcpy(&publicAddress.s_addr, ack + 16, 4);
        snprintf(s_onlinePublicAddress, sizeof(s_onlinePublicAddress), "%s:%u",
                 inet_ntoa(publicAddress), (unsigned)Get16(ack + 20));
        s_lastRegistryHeartbeat = osGetTime();
        return true;
    }

    static bool BeginMatchmakingSearch(const char *relayAddress)
    {
        const int fd = OpenTcpConnection(relayAddress, 5000);
        if (fd < 0)
            return false;
        u8 hello[kRegistryHelloSize];
        memset(hello, 0, sizeof(hello));
        memcpy(hello, "PN64", 4);
        hello[4] = kRegistryVersion;
        hello[5] = 3; // game-compatible matchmaking query
        memset(hello + 6, ' ', 8);
        EncodeGameIdentity(hello + 16);
        if (!SendBlocking(fd, hello, sizeof(hello)) || !SetNonBlocking(fd))
        {
            close(fd);
            return false;
        }
        if (s_registrySearchSocket >= 0)
            close(s_registrySearchSocket);
        s_registrySearchSocket = fd;
        s_registrySearchRxSize = 0;
        return true;
    }

    static void CloseOnlineSocket()
    {
        CloseOnlineServer();
        if (s_registrySocket >= 0)
        {
            close(s_registrySocket);
            s_registrySocket = -1;
        }
        if (s_registrySearchSocket >= 0)
        {
            close(s_registrySearchSocket);
            s_registrySearchSocket = -1;
            s_registrySearchRxSize = 0;
        }
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
        s_onlinePublicAddress[0] = '\0';
    }

    static bool ConnectOnline(const char *serverAddress, bool host, const char *roomCode)
    {
        if (!serverAddress || !serverAddress[0])
        {
            SetStatus("Enter host address[:port]");
            return false;
        }
        if (!s_socInitialized)
        {
            Result result = socInit(s_socBuffer, sizeof(s_socBuffer));
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
            SetStatus("Invalid host address");
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
                SetStatus("Invalid host port");
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
            SetStatus("Could not resolve host address");
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
            SetStatus("Could not connect to host");
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
        hello[13] = 0xFF;
        EncodeGameIdentity(hello + 14);
        u8 welcome[kOnlineWelcomeSize];
        if (!SendBlocking(fd, hello, sizeof(hello)) || !ReceiveBlocking(fd, welcome, sizeof(welcome)) ||
            memcmp(welcome, "PN64", 4) != 0 || welcome[4] != kOnlinePacketVersion || welcome[5] != 3)
        {
            close(fd);
            SetStatus("Host rejected code/game or timed out");
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

    static bool PollMatchmakingSearch()
    {
        if (s_registrySearchSocket < 0)
            return false;
        while (s_registrySearchRxSize < sizeof(s_registrySearchRx))
        {
            const ssize_t count = recv(s_registrySearchSocket,
                                       s_registrySearchRx + s_registrySearchRxSize,
                                       sizeof(s_registrySearchRx) - s_registrySearchRxSize, 0);
            if (count > 0)
                s_registrySearchRxSize += (size_t)count;
            else if (count == 0)
            {
                close(s_registrySearchSocket);
                s_registrySearchSocket = -1;
                CloseOnlineSocket();
                s_state = STATE_OFF;
                SetStatus("Matchmaking relay disconnected");
                return true;
            }
            else if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
                return true;
            else
            {
                close(s_registrySearchSocket);
                s_registrySearchSocket = -1;
                CloseOnlineSocket();
                s_state = STATE_OFF;
                SetStatus("Matchmaking relay connection failed");
                return true;
            }
        }

        u8 *result = s_registrySearchRx;
        close(s_registrySearchSocket);
        s_registrySearchSocket = -1;
        s_registrySearchRxSize = 0;
        if (memcmp(result, "PN64", 4) != 0 || result[4] != kRegistryVersion)
        {
            CloseOnlineSocket();
            s_state = STATE_OFF;
            SetStatus("Invalid matchmaking response");
            return true;
        }
        if (result[5] == 7)
        {
            CloseOnlineSocket();
            s_state = STATE_OFF;
            SetStatus("No compatible host found; try again");
            return true;
        }
        if (result[5] != 5 || Get16(result + 18) == 0)
        {
            CloseOnlineSocket();
            s_state = STATE_OFF;
            SetStatus("No compatible host found; try again");
            return true;
        }

        char roomCode[9];
        memcpy(roomCode, result + 6, 8);
        roomCode[8] = '\0';
        for (int i = 7; i >= 0 && roomCode[i] == ' '; --i)
            roomCode[i] = '\0';
        struct in_addr addr;
        memcpy(&addr.s_addr, result + 14, 4);
        char hostAddress[64];
        snprintf(hostAddress, sizeof(hostAddress), "%s:%u", inet_ntoa(addr),
                 (unsigned)Get16(result + 18));
        if (!ConnectOnline(hostAddress, false, roomCode))
        {
            s_state = STATE_OFF;
            SetStatus("Match found, but direct host connection failed");
            return true;
        }
        s_state = STATE_ONLINE_JOINED;
        snprintf(s_status, sizeof(s_status), "Matched: connected to room %.6s", roomCode);
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
    GenerateRoomCode(s_localRoomCode);
    memcpy(network.appdata, "PNL1", 4);
    memcpy(network.appdata + 4, s_localRoomCode, 6);
    EncodeGameIdentity(network.appdata + 10);
    network.appdata_size = kLocalAppDataSize;
    memset(&s_bindContext, 0, sizeof(s_bindContext));
    ClearInputs();
    Result rc = udsCreateNetwork(&network, NULL, 0, &s_bindContext,
                                 kDataChannel, UDS_DEFAULT_RECVBUFSIZE);
    if (R_FAILED(rc))
    {
        SetStatus("Could not start nearby room");
        return false;
    }

    if (!BindMedia(UDS_BROADCAST_NETWORKNODEID))
    {
        udsDestroyNetwork();
        return false;
    }
    s_nodeSlots[UDS_HOST_NETWORKNODEID] = (u8)CTRInput_GetLocalControllerPort();
    s_state = STATE_HOSTING;
    snprintf(s_status, sizeof(s_status), "Room %.6s - local matchmaking ready", s_localRoomCode);
    udsSetApplicationData(network.appdata, network.appdata_size);
    return true;
}

const char *GetLocalRoomCode()
{
    return s_localRoomCode;
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

static bool RoomCodeEquals(const char *input, const u8 *advertised)
{
    if (!input || strlen(input) != 6)
        return false;
    for (unsigned i = 0; i < 6; ++i)
    {
        char c = input[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if ((u8)c != advertised[i])
            return false;
    }
    return true;
}

bool JoinLocalByCode(const char *roomCode)
{
    if (!Scan())
        return false;
    for (size_t i = 0; i < s_roomCount; ++i)
    {
        const udsNetworkStruct &network = s_rooms[i].network;
        if (LocalRoomAvailable(network) && RoomCodeEquals(roomCode, network.appdata + 4))
            return Join(i);
    }
    SetStatus("No nearby compatible room has that code");
    return false;
}

bool JoinLocalMatchmaking()
{
    if (!Scan())
        return false;
    for (size_t i = 0; i < s_roomCount; ++i)
    {
        if (LocalRoomAvailable(s_rooms[i].network) && Join(i))
        {
            SetStatus("Match found - joined nearby room");
            return true;
        }
    }
    SetStatus("No compatible nearby rooms found - try again");
    return false;
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
    if (LocalRoomMatchesGame(s_rooms[roomIndex].network))
        snprintf(buffer, bufferSize, "%.6s %s (%u)",
                 (const char *)(s_rooms[roomIndex].network.appdata + 4), username,
                 (unsigned)s_rooms[roomIndex].network.total_nodes);
    else
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
    udsConnectionStatus connection;
    memset(&connection, 0, sizeof(connection));
    if (R_FAILED(udsGetConnectionStatus(&connection)) ||
        !BindMedia(connection.cur_NetworkNodeID))
    {
        udsDisconnectNetwork();
        return false;
    }
    s_state = STATE_JOINED;
    SetStatus("Joined nearby room - syncing controllers");
    return true;
}

bool HostOnline(const char *listenPort)
{
    if (s_state != STATE_OFF)
    {
        SetStatus("Stop the active session first");
        return false;
    }
    ClearInputs();
    if (!StartOnlineHost(listenPort))
        return false;

    const char *relay = GetMatchmakingRelayAddress();
    if (relay && relay[0] && strcmp(relay, "CHANGE_ME:5000") != 0 &&
        RegisterMatchmakingRoom(relay))
        snprintf(s_status, sizeof(s_status), "Room %.6s @ %.35s - matchmaking ready",
                 s_onlineRoomCode, s_onlinePublicAddress);
    else
    {
        s_onlinePublicAddress[0] = '\0';
        snprintf(s_status, sizeof(s_status), "Room %.6s - direct only; share host address",
                 s_onlineRoomCode);
    }
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
    snprintf(s_status, sizeof(s_status), "Joined room %s directly", s_onlineRoomCode);
    return true;
}

bool JoinOnlineMatchmaking(const char *relayAddress)
{
    if (s_state != STATE_OFF)
    {
        SetStatus("Stop the active session first");
        return false;
    }
    ClearInputs();
    if (!BeginMatchmakingSearch(relayAddress))
    {
        CloseOnlineSocket();
        SetStatus("Could not connect to matchmaking relay");
        return false;
    }
    s_state = STATE_ONLINE_MATCHMAKING;
    SetStatus("Searching relay; direct host connection follows");
    return true;
}

const char *GetMatchmakingRelayAddress()
{
    return CTR_MULTIPLAYER_MATCHMAKING_RELAY;
}

const char *GetOnlinePublicAddress()
{
    return s_onlinePublicAddress;
}

unsigned short GetOnlineListenPort()
{
    return s_onlineListenPort;
}

const char *GetOnlineRoomCode()
{
    return s_onlineRoomCode;
}

bool IsOnline()
{
    return s_state == STATE_ONLINE_HOSTING || s_state == STATE_ONLINE_JOINED ||
           s_state == STATE_ONLINE_MATCHMAKING;
}

bool SendMediaFrame(unsigned char type, const void *payload, size_t size)
{
    if ((s_state != STATE_HOSTING && s_state != STATE_JOINED) || !s_mediaBound ||
        size > CTRMediaFrame::kMaxPayload)
        return false;
    u8 wire[CTRMediaFrame::kMaxWireSize];
    const size_t wireSize = CTRMediaFrame::Encode(wire, sizeof(wire), type,
                                                   ++s_mediaSequence, payload, size);
    if (!wireSize || wireSize > UDS_DATAFRAME_MAXSIZE)
        return false;
    const Result rc = udsSendTo(UDS_BROADCAST_NETWORKNODEID, kMediaChannel,
                                UDS_SENDFLAG_Default, wire, wireSize);
    return R_SUCCEEDED(rc);
}

bool ReceiveMediaFrame(unsigned char *type, unsigned short *sequence,
                       unsigned short *sourceNode, void *payload,
                       size_t capacity, size_t *size)
{
    if (size)
        *size = 0;
    if (!s_mediaCount || !size)
        return false;
    const QueuedMediaFrame &frame = s_mediaQueue[s_mediaRead];
    *size = frame.size;
    if (!payload || capacity < frame.size)
        return false;
    if (type) *type = frame.type;
    if (sequence) *sequence = frame.sequence;
    if (sourceNode) *sourceNode = frame.source;
    if (frame.size)
        memcpy(payload, frame.payload, frame.size);
    s_mediaRead = (s_mediaRead + 1) % kMediaQueueCapacity;
    --s_mediaCount;
    return true;
}

void Stop()
{
    UnbindMedia();
    if (s_state == STATE_HOSTING)
        udsDestroyNetwork();
    else if (s_state == STATE_JOINED)
        udsDisconnectNetwork();
    if (IsOnline())
        CloseOnlineSocket();
    s_state = STATE_OFF;
    s_localRoomCode[0] = '\0';
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

    if (s_state == STATE_ONLINE_MATCHMAKING)
    {
        if (!PollMatchmakingSearch())
        {
            s_state = STATE_OFF;
            SetStatus("Matchmaking search stopped");
        }
        return;
    }
    if (s_state == STATE_ONLINE_HOSTING)
    {
        PollOnlineServer(localPad, outputPads);
        return;
    }
    if (s_state == STATE_ONLINE_JOINED)
    {
        if (!PollOnline(localPad, outputPads))
        {
            CloseOnlineSocket();
            s_state = STATE_OFF;
            ClearInputs();
            SetStatus("Host connection lost - session stopped");
        }
        return;
    }

    PumpUdsMedia();
    ReadPackets();
    if (s_state == STATE_OFF)
        return;
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

        // A joining node needs a snapshot as soon as its controller slot has
        // been assigned. Keep sending periodic full snapshots afterward so a
        // missed packet or stale remote state is repaired at least once/sec.
        if (s_stateSyncPending || now - s_lastStateSync >= kStateSyncIntervalMs)
        {
            Packet packet;
            memset(&packet, 0, sizeof(packet));
            packet.magic = kPacketMagic;
            packet.version = kProtocolVersion;
            packet.type = PACKET_STATE;
            packet.sequence = ++s_txSequence;
            memcpy(packet.nodeSlots, s_nodeSlots, sizeof(s_nodeSlots));
            EncodePads(packet, s_hostPads);
            SetPacketGameIdentity(packet);
            udsSendTo(UDS_BROADCAST_NETWORKNODEID, kDataChannel,
                      UDS_SENDFLAG_Default, &packet, sizeof(packet));
            s_lastStateSync = now;
            s_stateSyncPending = false;
        }
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
            SetPacketGameIdentity(packet);
            udsSendTo(UDS_HOST_NETWORKNODEID, kDataChannel,
                      UDS_SENDFLAG_Default, &packet, sizeof(packet));

            const u8 ownSlot = s_nodeSlots[connection.cur_NetworkNodeID];
            if (ownSlot < 4 && s_haveRxSequence)
            {
                memcpy(outputPads, s_hostPads, sizeof(s_hostPads));
                outputPads[ownSlot] = localPad[CTRInput_GetLocalControllerPort()];
            }
            else
            {
                // Do not run a temporary, differently-mapped local state while
                // waiting for the host's initial authoritative snapshot.
                memset(outputPads, 0, sizeof(OSContPad) * 4);
            }
        }
    }
}
}
