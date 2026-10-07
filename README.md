# DaedalusX64 / PN643D

## Build and ROM conversion

The native `romconvert` driver is a **single C source file**: `Tools/romconvert.c`. Compile that source once to the one `romconvert.exe` used for conversion and build commands:

```sh
gcc -O2 -std=c11 -Wall -Wextra -o romconvert.exe Tools/romconvert.c
```

On Windows, use MinGW-w64 GCC and run the equivalent command from the repository root. Keep the repository layout intact: the source includes the bundled LZ4 implementation from `Source/` at compile time. The executable still invokes external 3DS build tools (devkitPro/devkitARM, CMake, make, and the utilities under `Tools/`); those cannot be embedded into a C source file.

Put legally obtained N64 ROMs in `roms/`, then run `romconvert.exe` to convert them sequentially. Successful ROMs are moved to `used/`; CIA/3DSX packages are staged under `dist/`. Use `romconvert.exe --help` for available commands and options, including `setup`, `build-3ds`, `minimal`, `debug`, `downloadplay`, and `build-n64recomp`. `romconvert minimal` builds the main CIA with non-stick touchscreen interaction compiled out; the on-screen stick remains and is forced to N64 analog. Use `--minimal` with normal ROM conversion to build per-ROM CIAs in the same mode, or set `-DDAEDALUS_MINIMAL_EMULATOR=ON` in a direct CMake build. The virtual stick is larger in every build. The `recompile` command generates N64Recomp output; a game-specific adapter is needed to turn that output into a runnable 3DS app. See `Tools/ROMCONVERT_DEBUG.md` for command details and limitations.

## 3DS stereo head tracking

On supported New Nintendo 3DS hardware, nonminimal/non-Download-Play-child builds can use QTM eye tracking to shift the rendered 3D viewpoint as the player moves their head. Enable it on the new **Head tracking** options page and raise the hardware 3D slider. Tracking is optional, recenters when acquired, and does not capture camera frames directly: QTM manages the inner camera and IR emitter. Original 3DS models and SDKs without the libctru QTM API report the feature as unavailable. The effect is a conservative motion-parallax cue, not per-game camera control or a guarantee of correct geometry for every N64 title. Re-run `romconvert setup` so the updated bundled picaGL library is installed before rebuilding.

## Credits

cmf028: Major contributer of the ARM DynaRec code
rinnegatamante, xerpi: Porting DaedalusX64 to the Playstation Vita
TheFloW: Contributions to the DynaRec code
kreationz, salvy6735, Corn, Chilly Willy: Original DaedalusX64 code
Wally: Optimizations, improvements and ports
z2442: Compilation improvements and updating, optimizations
mrneo240: Optimizations, compilation help
TheMrIron2: Optimizations, wiki maintenance
MrHuu: Default DaedalusX64 Icon, banner
MasterFeizz: 3DS Port of DaedalusX64, PicaGL, and imgui-picagl
FFMpeg: for FFMpeg
Google: for Gemini
OpenAI: for GPT Luna
CodingFleet: for AI chat services
UnZip: for UnZip.exe
lz4: for lz4.exe and more lz4 usage stuff
devkitpro: for the devkitpro toolchain (and picasso)
