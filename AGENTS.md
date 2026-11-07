# ApoLite - working notes

A lightweight C++ clone of Equalizer APO. Windows 11 x64 only, no backwards
compatibility. MIT, Copyright 2026 Joel Mandell.

## Layout

* `include/apolite/`, `src/core/` - platform-independent DSP. This is where the
  tests point, so it must not depend on Windows.
* `src/win/` - the APO shell (COM, `APOProcess`).
* `src/gui/` - the editor.
* `src/cli/` - headless runner, useful for testing a config on Linux.
* `tools/bench.cpp` - cost benchmark, built only with `-DAPOLITE_BUILD_BENCH=ON`.

## Building and testing here

There is no cmake or MSVC in the dev container, so build the core directly:

```sh
g++ -O2 -std=c++20 -Wall -Wextra -Iinclude tests/test_apolite.cpp src/core/*.cpp -o /tmp/tests && /tmp/tests
g++ -O3 -std=c++20 -Iinclude tools/bench.cpp src/core/*.cpp -o /tmp/bench && /tmp/bench
```

CI (`.github/workflows/build.yml`) runs the core suite on Linux gcc and builds the
Windows targets on MSVC. Windows-only code is not compiled locally, so keep the
Windows targets building in CI, and do not add Windows headers to `src/core`.

## Contracts that are easy to break

* **Zero added latency.** `docs/architecture.md` and `README.md` both promise it,
  and `FirConvolver` is written to deliver it. Any change that buffers output
  (for example to handle odd block sizes with an output FIFO) breaks the promise
  and the `graphic EQ is transparent` test. Handle partial blocks by transforming
  against a zero-padded tail instead.
* **Allocation-free audio path.** All buffers are built at configure time. Do not
  allocate inside `process`/`APOProcess`. `FirConvolver` keeps scratch buffers
  (`savedFdlEntry_`) sized in the constructor for exactly this reason.
* **Any block size.** The host may ask for any number of frames. 480 (10 ms at
  48 kHz) is the normal Windows case and is not a multiple of the 256-frame
  convolver block. Tests must cover a non-multiple block size; a suite that only
  uses 4096/8192 will not catch a hang here.

## Releasing

The version is defined once, in `APOLITE_VERSION_*` in `include/apolite/apolite.h`.
CMake parses that header, fails the configure step if `project(ApoLite VERSION ...)`
does not match, and generates a `VERSIONINFO` resource from it for the two Windows
binaries (`resources/apolite.rc.in`). Bump the header and the `project()` line
together; `testVersionConsistency` also guards the string against the macros.

1. Bump `APOLITE_VERSION_PATCH`/`_STRING` in `include/apolite/apolite.h` and the
   `project(ApoLite VERSION ...)` line in `CMakeLists.txt`.
2. Push to `main` and wait for both CI jobs to pass.
3. Download the `ApoLite-windows` artifact from that run and package it - do not
   ship locally built binaries.
4. `gh release create` with `ApoLite-<ver>-win11-x64.zip` and `SHA256SUMS.txt`,
   built from the CI artifacts plus `LICENSE` and `config/voice-mic.txt`.

`gh` needs `GH_TOKEN` set from `GITHUB_TOKEN` in this environment.

Note: `APO_REG_PROPERTIES` carries only major/minor, so the patch digit is not
visible in the APO properties. It reaches the user through the VERSIONINFO
resource (file properties dialog) and the editor window title.
