# DaedalusX64 3DS source package

This repository contains DaedalusX64 source and 3DS assets, bundled picaGL and imgui-picagl source, and the native C++ `romconvert` command implementation. It does not include a game ROM, devkitPro SDK/toolchain, or a prebuilt release binary.

## Build the native romconvert executable

```sh
g++ -std=c++17 -O2 -Wall -Wextra -o romconvert.exe Tools/romconvert_aio.cpp
```

On Windows, run the single setup batch after compiling the executable:

```bat
deps\installdependencies.bat
```

The batch installs the SDK/build prerequisites and invokes `romconvert.exe setup` to build/install picaGL and imgui-picagl. The full 3DS configure/build flow is in the C++ driver:

```text
romconvert.exe build-3ds --jobs 4
```

Other integrated C++ commands include `setup`, `build-n64recomp`, `debug`, and `recompile`. See `ROMCONVERT_DEBUG.md` for use and limitations. No Python is used by `romconvert`; the only batch file is `deps/installdependencies.bat`.

## Source layout and provenance

- DaedalusX64 source: `layeraudioai/pn643d`, branch `3DS`, commit `1591b9f507a08474dc548e6b55bedabb76db3bc2` (`update v0.6900012`).
- picaGL source: `masterfeizz/picaGL`, branch `master`, commit `39039f3a40d4e5bc6c69aac96668401d121c002b`.
- picaGL is bundled under `deps/picaGL`; the CMake configuration supports this in-repository location and the historical sibling-directory location.

Install devkitPro/devkitARM and set `DEVKITPRO`/`DEVKITARM`. `build-3ds` uses `Tools/3dstoolchain.cmake` and the bundled 3DS packers. Keep legally obtained ROMs local; none are included here.

## Limitations

`romconvert recompile` requires a matching N64Recomp profile/ELF and a game-specific runtime/Daedalus adapter. It does not generically produce a playable native 3DS port. The ordinary emulator still runs standard N64 MIPS ROMs.

A successful compile is not equivalent to verification on hardware. This source package has not been validated using the devkitPro SDK in the sandbox. See `copying.txt` and dependency license files for licensing and attribution.
