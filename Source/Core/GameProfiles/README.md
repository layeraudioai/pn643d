# Compile-time game profiles

A normal build uses `DAEDALUS_TARGET_GAME_ID=0` and remains a general-purpose
emulator. For a dedicated build, `build_daedalus.sh` accepts an optional N64
ROM path or four-digit cartridge ID as its third argument. CMake passes that
ID to the compiler and includes `Core/GameProfiles/XXXX.h` when the file exists
(`XXXX` is the uppercase, zero-padded cartridge ID). Example:

```sh
./build_daedalus.sh CTR_RELEASE DaedalusX64 ./Roms/MyGame.z64
# or supply a CartID directly
./build_daedalus.sh CTR_RELEASE DaedalusX64 514D
```

A profile header is included by `Core/GameBuildConfig.h` in `ROM.cpp`. It may
provide `DAEDALUS_GAME_PROFILE_APPLY()` to adjust the current `g_ROM` settings
after the existing runtime compatibility database is applied. For example:

```cpp
// Core/GameProfiles/ABCD.h -- only compiled for cartridge ID 0xABCD
#define DAEDALUS_GAME_PROFILE_APPLY() \
    do { g_ROM.TLUT_HACK = true; } while (0)
```

Use this hook for verified compatibility fixes. For example, a memory-limited
title can opt in to a texture-cache entry cap after profiling confirms that its
working set permits it:

```cpp
#define DAEDALUS_GAME_PROFILE_APPLY() \
    do { g_ROM.settings.TextureCacheMaxEntries = 512; } while (0)
```

The limit evicts least-recently-used cache entries at display-list boundaries;
zero (the default) keeps the existing uncapped behavior. It limits cache-owned
entries, not textures still referenced by an in-flight renderer. A full-ROM-CRC
`TextureCacheMaxEntries` property can also be set in `roms.ini` for runtime
tuning without making a dedicated build.

Other translation units can use `DAEDALUS_GAME_BUILD_IS(0xABCD)` (or
`DAEDALUS_TARGET_GAME_ID`) in `#if` conditions to compile out irrelevant code
or enable profile-specific fast paths. Keep the default ID-zero path fully
supported: a ROM is still detected at runtime, and existing per-ROM hacks
remain active regardless of whether a dedicated profile was supplied. Avoid
disabling core emulator functionality unless the dedicated build is
intentionally restricted to that one game.

Only add per-game overrides when backed by testing on the affected title.
Profiles layer on top of runtime ROM-database settings and should not alter the
zero-ID/general build path.

The ROM argument is inspected only to obtain its cartridge ID; it is not
bundled into the application. ROM detection continues at runtime as before.
