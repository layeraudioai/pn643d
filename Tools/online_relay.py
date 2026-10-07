#!/usr/bin/env python3
"""PN643D matchmaking rendezvous service.

The relay carries no controller/gameplay packets. It keeps direct-host
registrations and returns a compatible host's public address to a matchmaking
client. All games are served by this one process; the ROM/region identity in the
hello packet is used to keep incompatible rooms apart.
"""

import argparse
import asyncio
import ipaddress
import logging
import os
from dataclasses import dataclass
from typing import Dict, Optional, Tuple

MAGIC = b"PN64"
VERSION = 1
HELLO_SIZE = 25
MATCH_SIZE = 20
ACK_SIZE = 22
PORT = int(os.environ.get("PN643D_RELAY_PORT", "5000"))
ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"


@dataclass
class HostRoom:
    code: str
    game_key: bytes
    address: str
    port: int
    writer: asyncio.StreamWriter


rooms: Dict[str, HostRoom] = {}
rooms_changed = asyncio.Condition()


def normalize_code(raw: bytes) -> Optional[str]:
    try:
        code = raw[:6].decode("ascii").upper()
    except UnicodeDecodeError:
        return None
    if len(code) != 6 or any(ch not in ALPHABET for ch in code):
        return None
    return code


def pack_ack(room: HostRoom) -> bytes:
    return (MAGIC + bytes((VERSION, 6, 0, 0))
            + room.code.encode("ascii").ljust(8, b" ")
            + ipaddress.IPv4Address(room.address).packed
            + room.port.to_bytes(2, "big"))


def compatible_room(game_key: bytes) -> Optional[HostRoom]:
    for room in rooms.values():
        if room.game_key == game_key and not room.writer.is_closing():
            return room
    return None


def pack_match(room: HostRoom) -> bytes:
    address = ipaddress.IPv4Address(room.address).packed
    return (MAGIC + bytes((VERSION, 5)) + room.code.encode("ascii").ljust(8, b" ")
            + address + room.port.to_bytes(2, "big"))


async def register_host(reader: asyncio.StreamReader, writer: asyncio.StreamWriter,
                        hello: bytes) -> None:
    code = normalize_code(hello[6:12])
    port = int.from_bytes(hello[14:16], "big")
    game_key = hello[16:25]
    peer = writer.get_extra_info("peername")
    if code is None or not (1 <= port <= 65535) or len(game_key) != 9 or not peer:
        return
    address = str(ipaddress.IPv4Address(peer[0]))
    if code in rooms:
        return
    room = HostRoom(code, game_key, address, port, writer)
    rooms[code] = room
    try:
        writer.write(pack_ack(room))
        await writer.drain()
        logging.info("room %s registered at %s:%d", code, address, port)
        async with rooms_changed:
            rooms_changed.notify_all()
        # The host sends a one-byte keepalive periodically. The TCP connection
        # itself is the lease: closing it withdraws the room immediately.
        while True:
            heartbeat = await reader.readexactly(1)
            if heartbeat != b"H":
                break
    finally:
        if rooms.get(code) is room:
            del rooms[code]
            logging.info("room %s withdrawn", code)
            async with rooms_changed:
                rooms_changed.notify_all()


async def wait_for_room(game_key: bytes, timeout: float = 120.0) -> Optional[HostRoom]:
    deadline = asyncio.get_running_loop().time() + timeout
    async with rooms_changed:
        while True:
            room = compatible_room(game_key)
            if room is not None:
                return room
            remaining = deadline - asyncio.get_running_loop().time()
            if remaining <= 0:
                return None
            try:
                await asyncio.wait_for(rooms_changed.wait(), timeout=remaining)
            except asyncio.TimeoutError:
                return None


async def client_connected(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    try:
        hello = await asyncio.wait_for(reader.readexactly(HELLO_SIZE), timeout=10)
        if hello[:4] != MAGIC or hello[4] != VERSION:
            return
        role = hello[5]
        if role == 1:
            await register_host(reader, writer, hello)
        elif role == 3:
            game_key = hello[16:25]
            room = await wait_for_room(game_key)
            if room is None or room.writer.is_closing():
                # Type 7 is an explicit no-compatible-room/timeout response.
                writer.write(MAGIC + bytes((VERSION, 7)) + bytes(MATCH_SIZE - 6))
            else:
                writer.write(pack_match(room))
                logging.info("matchmaking returned room %s to %s",
                             room.code, writer.get_extra_info("peername"))
            await writer.drain()
        else:
            logging.warning("unknown matchmaking role %d", role)
    except (asyncio.IncompleteReadError, asyncio.TimeoutError, ConnectionError,
            OSError, ValueError):
        pass
    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except (ConnectionError, asyncio.CancelledError):
            pass


async def serve(port: int) -> None:
    server = await asyncio.start_server(client_connected, "0.0.0.0", port, limit=1024)
    logging.info("PN643D matchmaking rendezvous listening on TCP %d", port)
    async with server:
        await server.serve_forever()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=PORT,
                        help="TCP port to listen on (default: %(default)s)")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s")
    try:
        asyncio.run(serve(args.port))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
