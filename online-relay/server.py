#!/usr/bin/env python3
"""Small self-hosted relay for DaedalusX64 3DS online controller input.

Run: python3 server.py --host 0.0.0.0 --port 37777
Expose TCP port 37777 on your router/firewall. Protocol v1 is intentionally
small and relays controller input only; it does not stream ROMs or game data.
"""
import argparse
import asyncio
import secrets
import socket
import struct
import time
from dataclasses import dataclass, field
from typing import Dict, Optional

MAGIC = b"PN64"
VERSION = 1
# Handshake: magic, version, kind (1=create, 2=join), room code (8 ASCII chars)
HELLO = struct.Struct("!4sBB8s")
# Reply: magic, version, kind (3=welcome, 4=error), player slot, reserved, room code
WELCOME = struct.Struct("!4sBBBB8s")
# Input: magic, version, kind=10, sequence, buttons, stick x, stick y
INPUT = struct.Struct("!4sBBHHbb")
# State: magic, version, kind=11, sequence, active slots mask, four pads
STATE_HEADER = struct.Struct("!4sBBHB")
PAD = struct.Struct("!Hbb")
MAX_PLAYERS = 4
IDLE_TIMEOUT = 60.0

@dataclass(eq=False)
class Peer:
    writer: asyncio.StreamWriter
    slot: int
    send_lock: asyncio.Lock = field(default_factory=asyncio.Lock)

@dataclass
class Room:
    code: str
    peers: Dict[int, Peer] = field(default_factory=dict)
    pads: list = field(default_factory=lambda: [(0, 0, 0) for _ in range(MAX_PLAYERS)])
    active_mask: int = 0
    sequence: int = 0
    last_activity: float = field(default_factory=time.monotonic)

rooms: Dict[str, Room] = {}
rooms_lock = asyncio.Lock()

async def send_state(room: Room) -> None:
    room.sequence = (room.sequence + 1) & 0xFFFF
    payload = STATE_HEADER.pack(MAGIC, VERSION, 11, room.sequence, room.active_mask)
    payload += b"".join(PAD.pack(*pad) for pad in room.pads)
    dead = []
    for slot, peer in list(room.peers.items()):
        try:
            async with peer.send_lock:
                peer.writer.write(payload)
                await asyncio.wait_for(peer.writer.drain(), timeout=2)
        except (ConnectionError, OSError, asyncio.TimeoutError):
            dead.append(slot)
    for slot in dead:
        await remove_peer(room, slot, False)
    if dead and room.peers and rooms.get(room.code) is room:
        await send_state(room)

async def remove_peer(room: Room, slot: int, announce: bool = True) -> None:
    peer = room.peers.pop(slot, None)
    room.active_mask &= ~(1 << slot)
    room.pads[slot] = (0, 0, 0)
    if peer:
        peer.writer.close()
    if slot == 0 or not room.peers:
        async with rooms_lock:
            rooms.pop(room.code, None)
        for remaining in room.peers.values():
            remaining.writer.close()
        room.peers.clear()
        room.active_mask = 0
    elif peer and announce:
        await send_state(room)

async def handle(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    transport_socket = writer.get_extra_info("socket")
    if transport_socket is not None:
        transport_socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    room: Optional[Room] = None
    slot: Optional[int] = None
    try:
        raw = await asyncio.wait_for(reader.readexactly(HELLO.size), timeout=8)
        magic, version, action, raw_code = HELLO.unpack(raw)
        if magic != MAGIC or version != VERSION or action not in (1, 2):
            return
        code = raw_code.decode("ascii", "ignore").strip().upper()
        async with rooms_lock:
            if action == 1:
                for _ in range(20):
                    code = "".join(secrets.choice("ABCDEFGHJKLMNPQRSTUVWXYZ23456789") for _ in range(6))
                    if code not in rooms:
                        break
                else:
                    return
                room = Room(code)
                rooms[code] = room
                slot = 0
            else:
                room = rooms.get(code)
                if room is None or len(room.peers) >= MAX_PLAYERS:
                    room = None
                else:
                    slot = next((candidate for candidate in range(1, MAX_PLAYERS)
                                 if candidate not in room.peers), None)
                    if slot is None:
                        room = None
        if room is None or slot is None:
            writer.write(WELCOME.pack(MAGIC, VERSION, 4, 0, 0, b"        "))
            await writer.drain()
            return
        peer = Peer(writer, slot)
        room.peers[slot] = peer
        room.last_activity = time.monotonic()
        writer.write(WELCOME.pack(MAGIC, VERSION, 3, slot, 0, room.code.encode("ascii").ljust(8, b" ")))
        await writer.drain()
        await send_state(room)

        while True:
            raw = await asyncio.wait_for(reader.readexactly(INPUT.size), timeout=IDLE_TIMEOUT)
            im, iv, kind, sequence, buttons, stick_x, stick_y = INPUT.unpack(raw)
            if im != MAGIC or iv != VERSION or kind != 10:
                break
            room.pads[slot] = (buttons, stick_x, stick_y)
            room.active_mask |= 1 << slot
            room.last_activity = time.monotonic()
            await send_state(room)
    except (asyncio.IncompleteReadError, asyncio.TimeoutError, ConnectionError, OSError):
        pass
    finally:
        if room is not None and slot is not None:
            await remove_peer(room, slot)
        writer.close()
        try:
            await writer.wait_closed()
        except (ConnectionError, OSError):
            pass

async def expire_rooms() -> None:
    while True:
        await asyncio.sleep(5)
        now = time.monotonic()
        expired = []
        async with rooms_lock:
            stale = [code for code, room in rooms.items()
                     if now - room.last_activity > IDLE_TIMEOUT]
            for code in stale:
                room = rooms.pop(code, None)
                if room is not None:
                    expired.append(room)
        for room in expired:
            for peer in room.peers.values():
                peer.writer.close()
            room.peers.clear()
            room.active_mask = 0

async def main(host: str, port: int) -> None:
    server = await asyncio.start_server(handle, host, port, reuse_address=True)
    print(f"DaedalusX64 relay listening on {host}:{port} (TCP)")
    asyncio.create_task(expire_rooms())
    async with server:
        await server.serve_forever()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0", help="bind interface (default: all)")
    parser.add_argument("--port", type=int, default=37777, help="TCP port (default: 37777)")
    args = parser.parse_args()
    try:
        asyncio.run(main(args.host, args.port))
    except KeyboardInterrupt:
        pass
