#!/usr/bin/env python3
"""Local protocol tests for the matchmaking-only rendezvous relay."""

import asyncio
import struct
import unittest

import online_relay as relay


class RelayProtocolTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        relay.rooms.clear()
        self.server = await asyncio.start_server(
            relay.client_connected, "127.0.0.1", 0, limit=1024
        )
        self.port = self.server.sockets[0].getsockname()[1]
        self.writers = []

    async def asyncTearDown(self):
        for writer in self.writers:
            writer.close()
        if self.writers:
            await asyncio.gather(*(w.wait_closed() for w in self.writers), return_exceptions=True)
        self.server.close()
        await self.server.wait_closed()
        relay.rooms.clear()

    async def register(self, code: bytes, game_key: bytes):
        reader, writer = await asyncio.open_connection("127.0.0.1", self.port)
        self.writers.append(writer)
        hello = bytearray(relay.HELLO_SIZE)
        hello[:4] = relay.MAGIC
        hello[4] = relay.VERSION
        hello[5] = 1
        hello[6:12] = code
        hello[12:14] = b"  "
        hello[14:16] = struct.pack(">H", 37777)
        hello[16:25] = game_key
        writer.write(hello)
        await writer.drain()
        ack = await reader.readexactly(relay.ACK_SIZE)
        self.assertEqual(ack[:6], relay.MAGIC + bytes((relay.VERSION, 6)))
        self.assertEqual(ack[16:20], bytes((127, 0, 0, 1)))
        self.assertEqual(struct.unpack(">H", ack[20:22])[0], 37777)
        return writer

    async def lookup(self, game_key: bytes):
        reader, writer = await asyncio.open_connection("127.0.0.1", self.port)
        self.writers.append(writer)
        hello = bytearray(relay.HELLO_SIZE)
        hello[:4] = relay.MAGIC
        hello[4] = relay.VERSION
        hello[5] = 3
        hello[6:14] = b" " * 8
        hello[16:25] = game_key
        writer.write(hello)
        await writer.drain()
        return await reader.readexactly(relay.MATCH_SIZE)

    async def test_one_instance_matches_by_game_and_returns_direct_endpoint(self):
        game_a = b"SM64CRC1U"
        game_b = b"ZELDCRC2J"
        await self.register(b"ABC234", game_a)
        await self.register(b"DEF567", game_b)

        match_a = await self.lookup(game_a)
        match_b = await self.lookup(game_b)
        self.assertEqual(match_a[:6], relay.MAGIC + bytes((relay.VERSION, 5)))
        self.assertEqual(match_a[6:12], b"ABC234")
        self.assertEqual(match_a[14:18], bytes((127, 0, 0, 1)))
        self.assertEqual(struct.unpack(">H", match_a[18:20])[0], 37777)
        self.assertEqual(match_b[6:12], b"DEF567")

        # The rendezvous response is only an endpoint handoff, never a game-state
        # packet stream. Closing a host registration withdraws its room.
        self.assertEqual(len(relay.rooms), 2)
        self.writers[0].close()
        await self.writers[0].wait_closed()
        for _ in range(20):
            if "ABC234" not in relay.rooms:
                break
            await asyncio.sleep(0.01)
        self.assertNotIn("ABC234", relay.rooms)


if __name__ == "__main__":
    unittest.main()
