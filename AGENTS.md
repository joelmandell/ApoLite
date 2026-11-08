# ApoLite - working notes

A lightweight C++ clone of Equalizer APO. Windows 11 x64 only, no backwards
compatibility. MIT, Copyright 2026 Joel Mandell.

## Layout

* `include/apolite/`, `src/core/` - platform-independent DSP. This is where the
  tests point, so it must not depend on Windows. `registry.cpp` is the exception
  that proves the rule: it holds the Windows endpoint registry layout as data, so
  the decisions can be tested on Linux and only the writes are Windows-only.
* `src/win/` - the APO shell (COM, `APOProcess`).
* `src/gui/` - the editor.
* `src/setup/` - `ApoLiteSetup.exe`, which attaches the APO to an audio endpoint.
  Windows-only. `win_registry.cpp` needs `aclapi.h` for the ownership change.
* `src/cli/` - headless runner, useful for testing a config on Linux.
* `tools/bench.cpp` - cost benchmark, built only with `-DAPOLITE_BUILD_BENCH=ON`.

## Building and testing here

`cmake` is not on `PATH` but is installed at `~/.local/bin/cmake` (pip). A mingw
cross-compiler is installed, so the Windows targets can be compiled locally
instead of waiting on CI:

```sh
# core suite, fast
g++ -O2 -std=c++20 -Wall -Wextra -Iinclude tests/test_apolite.cpp src/core/*.cpp -o /tmp/tests && /tmp/tests

# every Windows target, including the APO, editor and setup tool
~/.local/bin/cmake -B /tmp/mb -S . -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres -DAPOLITE_BUILD_TESTS=OFF
~/.local/bin/cmake --build /tmp/mb --parallel
```

The mingw build is not a substitute for MSVC: it does not catch MSVC-only
warnings, and `-municode` is needed for the `wmain` entry point. Use it to catch
compile errors early, and still let CI be the gate.

CI (`.github/workflows/build.yml`) runs the core suite on Linux gcc and builds the
Windows targets on MSVC. Do not add Windows headers to `src/core`.

## Contracts that are easy to break

* **An APO must be attached to a device, not just registered.** `regsvr32` writes
  the COM class and calls `RegisterAPO`; neither makes Windows load the APO.
  Windows loads it only when the endpoint's own key names it, under
  `MMDevices\Audio\{Render|Capture}\<endpoint GUID>\FxProperties`. That
  association is what `ApoLiteSetup` writes, and without it the APO never shows
  up in the Enhancements tab. `include/apolite/registry.h` documents the value
  names; `testRegistryLayout` covers the decisions. Also required:
  `DisableProtectedAudioDG = 1` under `...\CurrentVersion\Audio`, or the engine
  refuses to load an unsigned APO.
* **Do not read the live FxProperties key to decide the install mode.** Once
  installed it holds ApoLite's own CLSIDs, so a reinstall would pick a different
  mode and orphan the slot already owned. Use the saved originals
  (`HKLM\SOFTWARE\ApoLite\Child APOs`); `driverSlotPresent` does this.
* **Zero added latency.** `docs/architecture.md` and `README.md` both promise it,
  and `FirConvolver` is written to deliver it. Any change that buffers output
  (for example to handle odd block sizes with an output FIFO) breaks the promise
  and the `graphic EQ is transparent` test. Handle partial blocks by transforming
  against a zero-padded tail instead.
* **Allocation-free audio path.** All buffers are built at configure time. Do not
  allocate inside `process`/`APOProcess`. `FirConvolver` keeps scratch buffers
  (`savedFdlEntry_`) sized in the constructor for exactly this reason.
* **Share one kernel per impulse, and bound its length by memory.** A `FirKernel`
  holds the impulse spectra and is immutable once built; every channel that
  convolves with the same impulse shares one `FirKernel` through a `shared_ptr`.
  Do not go back to constructing a `FirConvolver` from taps per channel: that
  multiplies the spectra (roughly eight times the impulse length) by the channel
  count. Length is bounded in bytes, not taps: `firMemoryBytes` is the estimate,
  `maxImpulseTapsForBudget` its inverse, and the config loader rejects an
  over-budget file from its WAV header before reading a sample. `FirKernel` also
  refuses to build past `kMaxFirKernelBytes`, an overflow guard that holds even
  when the user raises `ImpulseBudgetMB`. The loader's rejection names the file,
  its length and the memory it would have taken; `Engine::loadConfiguration`
  drops a filter whose `prepare` throws rather than letting the exception escape,
  because reloads happen on the watcher thread where an uncaught exception
  terminates the process.
* **Any block size.** The host may ask for any number of frames. 480 (10 ms at
  48 kHz) is the normal Windows case and is not a multiple of the 256-frame
  convolver block. Tests must cover a non-multiple block size; a suite that only
  uses 4096/8192 will not catch a hang here.

## Releasing

The version is defined once, in `APOLITE_VERSION_*` in `include/apolite/apolite.h`.
CMake parses that header, fails the configure step if `project(ApoLite VERSION ...)`
does not match, and generates a `VERSIONINFO` resource from it for the three
Windows binaries (`resources/apolite.rc.in`). Bump the header and the `project()`
line together; `testVersionConsistency` also guards the string against the macros.

1. Bump `APOLITE_VERSION_PATCH`/`_STRING` in `include/apolite/apolite.h` and the
   `project(ApoLite VERSION ...)` line in `CMakeLists.txt`.
2. Push to `main` and wait for both CI jobs to pass.
3. Download the `ApoLite-windows` artifact from that run and package it - do not
   ship locally built binaries.
4. `gh release create` with `ApoLite-<ver>-win11-x64.zip` and `SHA256SUMS.txt`,
   built from the CI artifacts plus `LICENSE`, `config/voice-mic.txt` and a
   `README.txt` written for the release. The zip is flat: the three binaries at
   the top level, then `config/`, `LICENSE`, `README.txt`. Give every member a
   fixed timestamp and mode so the zip is reproducible.

`gh` needs `GH_TOKEN` set from `GITHUB_TOKEN` in this environment.

The release tag is annotated and points at the merge commit on `main`, not at a
local commit, so `git ls-remote --tags` and the release agree. Confirm the
peel with `gh api repos/<owner>/<repo>/commits/v<ver> --jq .sha`.

Check the packaged binaries really are the version they claim: the version
string is UTF-16 in the `VERSIONINFO` resource, so a plain ASCII grep for
`1.0.5` misses it. Search for `'1.0.5'.encode('utf-16-le')` instead.

The PR that is released must be marked ready first; the merge API refuses a
draft with `Pull request is in draft`. Use `gh pr ready <n>` before merging.

Note: `APO_REG_PROPERTIES` carries only major/minor, so the patch digit is not
visible in the APO properties. It reaches the user through the VERSIONINFO
resource (file properties dialog) and the editor window title.
