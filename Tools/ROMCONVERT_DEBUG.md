# Batch debug builds with `romconvert debug`

`romconvert_aio.c` is a host-side build driver. It scans the project's top-level
`roms/` directory for raw `.z64`, `.n64`, and `.v64` N64 images, reads each
header's CartID (accounting for the three byte orders), then configures and
builds one CTR diagnostic build per unique CartID. ROMs sharing an ID reuse the
same build.

Each CMake configuration enables `DAEDALUS_ENABLE_SDMC_DIAGNOSTICS` (which
also enables function profiling) and `DAEDALUS_ENABLE_DEBUG_CONSOLE`. The CTR
build still uses the project's normal optimized release toolchain settings;
this is an instrumented/debug-diagnostics build, not a slow host build.

## Windows

1. Install host **MinGW-w64 GCC**, CMake, and the devkitPro 3DS toolchain and
   libraries required by this project.
2. Place raw ROM images in `roms/` at the repository root.
3. Run `Tools\build_romconvert_aio.bat` once to compile/install the host helper.
   It backs up the old root `romconvert.exe` to `Tools\romconvert-legacy.exe`
   (if not already backed up), then installs the new helper as `romconvert.exe`.
4. From the repository root, run:

   ```bat
   romconvert debug
   ```

## Linux/macOS

Install a C compiler and CMake, then from the project root run:

```sh
sh Tools/build_romconvert_aio.sh
./romconvert debug
```

The 3DS toolchain must be configured as usual (including `DEVKITPRO`). The
helper invokes `cmake --build ... --target N3DS --parallel 2`; each unique ID
is configured under `build/debug/cart_XXXX/`. Finished `.3dsx`, `.cia`, and
`.elf` files are copied, when present, to `dist/debug/<rom-name>_XXXX/`.
The ROM files themselves are not copied into the output folders. `build/` and
`dist/` can be deleted to reclaim generated data; subsequent runs reuse CMake
build directories.

To inspect which commands would run without launching CMake or compiling, set
`ROMCONVERT_DRY_RUN=1` before invoking the helper. This still requires at least
one valid raw ROM in `roms/`.

The driver reports malformed ROM headers and build failures and continues with
other inputs. It does not include ROM images in the source ZIP or distribute
any copyrighted ROM content. Archive/compressed ROMs are not scanned; extract
them into one of the supported raw formats first.
