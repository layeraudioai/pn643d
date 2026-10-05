# Native `romconvert` toolchain

The command dispatcher and build orchestration are implemented in `Tools/romconvert_aio.cpp`. The only remaining batch file is `deps/installdependencies.bat`; there are no shell-script or per-task batch wrappers in `Tools/`.

## Build the one `romconvert.exe`

From the repository root, using a C++17 compiler:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -o romconvert.exe Tools/romconvert_aio.cpp
```

On Windows, use MinGW-w64 g++ or another C++17 compiler. The setup batch does not compile or replace `romconvert.exe`; compile it yourself as requested, then run the setup file.

## Initial 3DS dependency setup

Install devkitPro/devkitARM first. Then run this one batch file from Windows:

```bat
deps\installdependencies.bat
```

It updates/installs the devkitPro 3DS SDK and build tools, then calls `romconvert.exe setup`. That command builds and installs the repository's bundled picaGL and imgui-picagl libraries. It does not download game ROMs or N64Recomp profiles.

## Commands

```text
romconvert.exe setup                 Build/install picaGL and imgui-picagl
romconvert.exe build-3ds             Configure and build DaedalusX64 for 3DS
romconvert.exe build-n64recomp       Fetch/build the optional host N64Recomp CLI
romconvert.exe debug                 Build diagnostic Daedalus output grouped by CartID
romconvert.exe recompile             Run N64Recomp plus the configured game-specific 3DS adapter
romconvert.exe downloadplay --rom PATH Build compressed-ROM child CIA plus bundled host CIA
```

Use `--jobs N` to control CMake parallelism, `--root PATH` to select a checkout, and `--dry-run` to print build commands. `build-3ds` uses `Tools/3dstoolchain.cmake`; set `DEVKITPRO` and `DEVKITARM` for the devkitPro installation.

For example:

```bat
romconvert.exe build-3ds --jobs 4
```

The generated build tree and outputs are under `build/3ds`. Packaging relies on the repository's existing 3DS tools in `Tools/`.

## ROM-specific Download Play package

```text
romconvert.exe downloadplay --rom roms/example.z64 --jobs 4
```

This mode creates a Daedalus LZ4 ROM image, builds a `DlpChild` CIA with that ROM under `romfs:/Roms/`, checks the child CIA against the 32 MiB transfer ceiling, and builds a regular host CIA with `downloadplay-child.cia` bundled in its RomFS. The child build now starts directly into its bundled ROM and exits back to the system title launcher when emulation ends. Outputs are staged in `dist/downloadplay/<rom-name>/`. The ROM must be one you are authorized to use; no ROM is included in the repository.

The host now has an experimental `dlp:SRVR` lifecycle path: it checks that `romfs:/downloadplay-child.cia` is present and within 32 MiB, initializes the server IPC session, opens accepting on a local wireless channel, accepts incoming nodes, sends a wireless-reboot passphrase, and invokes `StartDistribution`. The in-game Multiplayer page exposes this flow only when the host was built with a bundled child CIA. The service is finalized on stop/exit.

**This is not yet a verified end-to-end transmission.** The public service descriptions do not specify the required shared-memory sizing/permissions and event semantics in enough detail, and `StartDistribution` has no documented CIA path parameter. The code therefore uses an inferred pair of 0x40000 shared transfer blocks and assumes the DLP system service resolves the indexed child title; whether that service can consume the CIA bundled in the host RomFS is unverified. The reported client progress layout/state is also inferred. If the service only locates an installed or otherwise registered child title, the RomFS bundle alone will not be distributed. Test service access, IPC handle ownership, child-title indexing, payload resolution, distribution, system-app launch, and return behavior on a console before treating this as working. No access restrictions are bypassed.

The child still uses the full Daedalus application, rather than a stripped runtime. Its direct-boot path and clean return behavior remain untested on a 3DS. A successful CIA build is not proof of DLP compatibility or stock-console acceptance.

`--dry-run` prints the planned child build without compressing the ROM, building a CIA, or performing the size check.

## N64Recomp recompile flow

`romconvert build-n64recomp` clones the public N64Recomp repository into `deps/N64Recomp` when needed, configures it with CMake, and builds `N64RecompCLI`. The host needs Git, CMake, and a C++ compiler. Set `N64RECOMP_SOURCE`, `N64RECOMP_BUILD`, `N64RECOMP_GENERATOR`, or `N64RECOMP` to override defaults.

`romconvert recompile` still needs a matching game ROM, N64Recomp profile/ELF metadata, and a game-specific integration adapter. Example:

```text
romconvert.exe recompile --rom roms/example.z64 \
  --profile recomp/example/recomp.toml \
  --3ds-adapter ports/example/daedalus-3ds.cmake \
  --3ds-toolchain Tools/3dstoolchain.cmake
```

This path compiles generated code for the 3DS ARM target; it cannot turn an ARM ELF into a standard N64 ROM. The adapter must supply the game/runtime integration and package a functioning 3DS app. A profile/source-only run is not a complete playable port. The current repository does not supply arbitrary title-specific profiles or adapters.

## Dependencies and files

- C++ command logic: `Tools/romconvert_aio.cpp`
- One Windows setup file: `deps/installdependencies.bat`
- CMake 3DS toolchain: `Tools/3dstoolchain.cmake`
- Bundled picaGL and ImGui sources: `deps/picaGL`, `deps/imgui-picagl`

No Python is used by the `romconvert` toolchain.
