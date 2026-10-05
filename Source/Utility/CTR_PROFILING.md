# CTR diagnostics and function profiling

For a diagnostic 3DS build that writes debug events and periodic function-profiler snapshots to the SD card:

```sh
cmake -S . -B build-ctr -DCTR_RELEASE=ON -DDAEDALUS_ENABLE_SDMC_DIAGNOSTICS=ON
cmake --build build-ctr --target N3DS
```

After launching the app, retrieve:

```text
sdmc:/3ds/DaedalusX64/diagnostics.log
```

The diagnostic option automatically enables the built-in function profiler and the emulator's logging. The log is opened fresh for each launch and flushed line-by-line, so it should retain useful information after a crash or forced exit. It records high-level subsystem diagnostics, system/ROM component initialization and teardown, and profile snapshots every 300 VBlanks (about five seconds at 60 Hz). Profile rows include the call path depth, function label, elapsed milliseconds, parent/overall percentages, and hit count. Existing on-screen console output remains enabled in development builds.

This mode is intentionally intrusive: profiling and SD writes affect timing. Use it to identify candidates, then benchmark a release build with diagnostics disabled. The default diagnostic mask captures subsystem, interrupt, dynarec, and frame events, but omits per-memory-access and register tracing because those can generate enormous logs and overwhelm the SD card. The file can still grow with long sessions; stop once you have a representative workload, then share `diagnostics.log`.

## Function coverage and profiling

The built-in profiler reports functions that contain `DAEDALUS_PROFILE` scopes; an unlisted function is not evidence that it is fast. Current instrumentation covers display-list processing, texture-cache lookup/update/hash/conversion, renderer flush/triangle preparation, HLE audio, and RSP task dispatch. Compare multiple representative runs and prioritize by overall percentage and total time, not just per-call time.

For function profiling without the SDMC logging layer, use `-DDAEDALUS_ENABLE_FUNCTION_PROFILING=ON`. That option enables the profiler but does not by itself turn on SD-card diagnostics.

## Current ARM11 optimization candidate

`TextureInfo::GenerateHashValue` samples texture words using one of three row counts (5, 49, or 1000). The old expression divided by a runtime variable; ARM11/ARMv6K has no integer divide instruction, so that can invoke a software division helper. The selected divisor is now explicit in a `switch`, allowing the compiler to lower each case as a constant-divisor multiply/shift sequence. The existing fallback preserves behavior if another row count is added later.

This source-level optimization is targeted, not a measured claim. Confirm its impact on hardware with the `TextureInfo::GenerateHashValue` profile entry and retain it only if representative workloads improve without changing rendered output.
