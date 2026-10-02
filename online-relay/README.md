# DaedalusX64 online controller relay

The online relay exchanges controller input only: **each player must already be running the same ROM and compatible emulator build**. It does not synchronize emulation frames, transfer ROMs, or provide matchmaking beyond a short room code.

## Host from a 3DS

Choose **Host Online** in the 3DS in-game multiplayer menu. The 3DS runs the TCP relay itself; no PC/server is needed as a middleman. Enter a listening port (normally `37777`), then share the displayed six-character room code and your public DNS name or IPv4 address with friends. Forward that **TCP** port on your router to the 3DS's private IP and allow it in any firewall. For a same-LAN test, join using the host 3DS's private IPv4 address and port.

The relay starts only for online hosting. Offline emulation and nearby/local wireless sessions do not start the Internet relay.

## Optional PC relay

The included Python server remains available for users who prefer hosting on a PC or server:

```sh
python3 server.py --host 0.0.0.0 --port 37777
```

Forward/allow the TCP port on that server, and give players its public hostname/IP, port, and the generated room code. The PC server uses the same v1 wire protocol and works with the 3DS client.

## Security and limitations

- The v1 protocol is plain TCP with no authentication or encryption. Use only a relay you control and trust; do not expose it to untrusted players. A TLS reverse proxy alone is **not** sufficient because the client speaks raw TCP, not TLS.
- Room codes are temporary identifiers, not authentication. Rooms support at most four consoles and close when the host disconnects.
- TCP can add latency and head-of-line stalls; this is a basic self-hosted implementation, not rollback netplay. Emulation timing and game-state desync are not corrected by the relay.
- The 3DS must be connected to Wi-Fi with Internet access. Clients currently support IPv4 hostnames/addresses.

## Protocol

The implementation uses a compact fixed-size binary protocol in `server.py` and `daed/Source/SysCTR/Input/CTRMultiplayer.cpp`. Keep their version and packet layouts in sync when changing the wire protocol.
