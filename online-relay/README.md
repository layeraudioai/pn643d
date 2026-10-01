# DaedalusX64 self-hosted online relay

This small Python 3 relay allows 3DS clients to exchange controller state over the Internet. It relays input only: **each player must already be running the same ROM and compatible emulator build**. It does not synchronize emulation frames, transfer ROMs, or provide matchmaking beyond a short room code.

## Run the relay

On a server with Python 3.9+:

```sh
python3 server.py --host 0.0.0.0 --port 37777
```

Allow inbound **TCP** port `37777` in the server firewall and, if hosted behind a router, forward that port to the server. Give the public DNS name or IPv4 address (and port, if not 37777) to players. For example, enter `games.example.net:37777` on the 3DS. Hosting creates a six-character room code; joining players enter the same relay address and that code.

For a test on the same LAN, use the relay computer's private IPv4 address. The relay binds to all interfaces by default. Press Ctrl+C to stop it.

## Security and limitations

- The v1 protocol is plain TCP and has no authentication or encryption. Use only a relay you control and trust; do not expose it to untrusted players. A TLS reverse proxy alone is **not** sufficient because the 3DS client currently speaks raw TCP, not TLS.
- Room codes are random, temporary, and expire when the host disconnects or the room is idle. Rooms support at most four consoles/players.
- TCP can add latency and head-of-line stalls; this is a basic self-hosted implementation, not rollback netplay. Emulation timing and game-state desync are not corrected by the relay.
- On the 3DS, connect to the Internet using the system's configured Wi-Fi. The client currently supports IPv4 relay addresses/hostnames.

## Protocol

The implementation uses a compact fixed-size binary protocol in `server.py` and `CTRMultiplayer.cpp`. Keep their version and packet layouts in sync when changing the wire protocol.
