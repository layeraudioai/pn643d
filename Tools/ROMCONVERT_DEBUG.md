# Unified `romconvert` C driver

`Tools/romconvert.c` is the single source file for the `romconvert` executable. There is no separate C++ dispatcher or batch launcher required. Compile it from the repository root:

```sh
gcc -O2 -std=c11 -Wall -Wextra -o romconvert.exe Tools/romconvert.c
```

For Windows, use MinGW-w64 GCC. The bundled LZ4 sources are included directly by the C source, so compile with the checkout's directory structure intact. This produces one host-side executable; it does not statically contain devkitPro, CMake, GNU make, or the external packaging utilities.

## Commands

```text
romconvert.exe                         Convert ROMs in roms/ to per-ROM packages
romconvert.exe debug                   Same conversion with diagnostics/profiling enabled
romconvert.exe setup                   Install bundled picaGL and imgui-picagl libraries
romconvert.exe build-3ds --jobs 4      Build DaedalusX64 for 3DS
romconvert.exe minimal                 Build the main CIA with minimal touchscreen controls
romconvert.exe downloadplay --rom PATH Build a ROM-specific child CIA and bundled host CIA
romconvert.exe build-n64recomp         Fetch/build the optional N64Recomp CLI
romconvert.exe recompile --rom PATH --profile FILE
romconvert.exe --help                  Show options
```

ROM conversion accepts `--roms DIR` (default `roms/`), `--seed N`, and `--minimal` to compile out non-stick touchscreen controls for each per-ROM CIA. `romconvert minimal` builds the main CIA with `DAEDALUS_MINIMAL_EMULATOR=ON`; direct CMake builds can enable the same option. In minimal builds the larger virtual stick remains enabled and always routes to the N64 analog stick. `--scan DIR` inspects ROM headers without building. Successful input ROMs are moved to `used/`; outputs are placed in `dist/`. Use `--root PATH` when running the executable outside the checkout and `--jobs N` to set parallel build count. `--dry-run` prints planned commands for build/setup modes; the full ROM conversion is not a dry-run operation.

## Dependencies and limitations

A 3DS build still needs devkitPro/devkitARM, CMake, GNU make, and the repository's packaging tools in `Tools/` (for example 3dstool, bannertool, makerom, and 3dsxtool). `ffmpeg` is optional for preview/banner preparation. `setup` additionally needs `DEVKITPRO` set and the bundled dependency Makefiles.

`downloadplay` creates a compressed ROM, builds a child CIA, enforces the 32 MiB child-CIA size ceiling, and bundles it into a host CIA. The Download Play service/transmission path is experimental and is not verified end-to-end on hardware.

`recompile` runs N64Recomp to generate native source output. Generated ARM code is not an N64 ROM; a game-specific runtime/Daedalus integration adapter is required to produce a playable 3DS app. Building N64Recomp does not provide game profiles or adapters.
