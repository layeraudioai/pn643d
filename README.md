# DaedalusX64

DaedalusX64 is an N64 emulator port for Nintendo 3DS. See `Data/readme.txt`
for usage, supported formats, and compatibility notes.

## Build and debug ROM profiles

For an instrumented CTR build for every raw ROM in the root `roms/` folder,
see [`Tools/ROMCONVERT_DEBUG.md`](Tools/ROMCONVERT_DEBUG.md). Once the host
helper is built, the command is `romconvert debug` from the repository root.

The repository also contains legacy Windows ROM conversion utilities; the
batch debug helper preserves the original root converter as
`Tools/romconvert-legacy.exe` before replacing the root command.

## 3DS build prerequisites

Install/configure devkitPro/devkitARM and the project dependencies described in
`deps/`. The CTR build is configured through CMake with the toolchain in
`Tools/3dstoolchain.cmake`.

> `boot.firm` is an experimental modified LumaCFW image. It is not required to
> build or run DaedalusX64 and should only be used if you understand the risks.

## Credits
-
- cmf028: Major contributer of the ARM DynaRec code
- rinnegatamante, xerpi: Porting DaedalusX64 to the Playstation Vita
- TheFloW: Contributions to the DynaRec code
- kreationz, salvy6735, Corn, Chilly Willy: Original DaedalusX64 code
- Wally: Optimizations, improvements and ports
- z2442: Compilation improvements and updating, optimizations
- mrneo240: Optimizations, compilation help
- TheMrIron2: Optimizations, wiki maintenance
- MrHuu: Default DaedalusX64 Icon, banner
- MasterFeizz: 3DS Port of DaedalusX64, PicaGL, and imgui-picagl
- FFMpeg: for FFMpeg
- Google: for Gemini
- OpenAI: for GPT Luna
- CodingFleet: for AI chat services
- UnZip: for UnZip.exe
- lz4: for lz4.exe and more lz4 usage stuff
-
## Credits
-
- cmf028: Major contributer of the ARM DynaRec code
- rinnegatamante, xerpi: Porting DaedalusX64 to the Playstation Vita
- TheFloW: Contributions to the DynaRec code
- kreationz, salvy6735, Corn, Chilly Willy: Original DaedalusX64 code
- Wally: Optimizations, improvements and ports
- z2442: Compilation improvements and updating, optimizations
- mrneo240: Optimizations, compilation help
- TheMrIron2: Optimizations, wiki maintenance
- MrHuu: Default DaedalusX64 Icon, banner
- MasterFeizz: 3DS Port of DaedalusX64, PicaGL, and imgui-picagl
- FFMpeg: for FFMpeg
- Google: for Gemini
- OpenAI: for GPT Luna
- CodingFleet: for AI chat services
- UnZip: for UnZip.exe
- lz4: for lz4.exe and more lz4 usage stuff
-
