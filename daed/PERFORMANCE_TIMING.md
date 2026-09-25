# Uncapped and adaptive timing (CTR / 3DS)

The 3DS video limiter no longer sleeps to hold output to a nominal refresh rate. The legacy `SpeedSyncEnabled` configuration field remains readable/writable so old preference files still load, but it has no effect on video pacing. The in-game menu now reports that the cap is removed.

The region-rate table follows the requested order and values **60, 50, 60 Hz** (PAL, NTSC, MPAL). These values describe guest video timing; they are not a host frame-rate limit.

At each rendered VI-origin change, the limiter measures elapsed `svcGetSystemTick()` ticks and smooths a performance ratio. The ratio is bounded to 0.25x–8x and updated continuously. The next guest VI event's emulated CPU-cycle budget follows this ratio, so the target clock and guest tick cadence adapt at runtime. `FramerateLimiter_GetTargetClockRateHz()` reports the ROM's target clock (or 93.75 MHz when the ROM header has no clock) multiplied by that ratio.

The host timer remains the fixed monotonic libctru system-tick source; it is deliberately not scaled, because elapsed wall time must remain truthful. `FramerateLimiter_GetHostClockRateHz()` separately reports an effective throughput estimate using a 268 MHz original-3DS baseline or an 804 MHz New-3DS baseline, adjusted by the measured performance ratio. This is an estimate, not a request to overclock hardware. Guest and host nominal clocks are kept separate.

The codebase is already predominantly C++. The CTR-only `MemoryCTR.c` translation unit was converted to C++ (`MemoryCTR.cpp`) and its CMake source list updated. Vendored C libraries, PSP-specific C components, and assembly remain in their original languages rather than being mechanically renamed, which would break their toolchains/ABI without constituting a meaningful C++ conversion.
