# ApoLite

A very small, very fast system-wide audio equalizer for **Windows 11 only**.

ApoLite is a from-scratch C++ rewrite of the idea behind
[Equalizer APO](https://sourceforge.net/projects/equalizerapo/), built with a
narrow goal: boost and clean up the audio you send through a conferencing app
such as Zoom, using as little CPU and memory as possible.

## Why another one

Equalizer APO is powerful but heavy: a large configuration surface, a Qt editor,
and a processing graph that carries features most people never touch. ApoLite
keeps the configuration language people already know, ships a small editor for
the settings that matter, and targets one platform.

| | Equalizer APO | ApoLite |
|---|---|---|
| Target OS | Windows 7+ | Windows 11 only |
| Runtime dependency | Qt, VCL, several helper DLLs | none (static runtime) |
| DSP | multi-threaded graph | single real-time chain, no locks on the audio thread |
| Config language | rich | the common subset (see below) |
| Editor | full Qt GUI, ~40 MB | one small window, no UI framework |
| Installed size | tens of MB | about 1 MB (DLL plus editor) |

## Features

* Parametric and shelf EQ: `PK`, `LP`, `HP`, `BP`, `LPQ`, `HPQ`, `LSC`, `HSC`,
  `LS`, `HS`, `NO`, `AP`, `IIR`.
* `GraphicEQ` and `Convolution` via a minimum-phase FIR and a partitioned
  convolver (zero added latency).
* `Preamp`, `Delay`, `Copy`, `Channel`, `Device`, `Stage`, `Include`, and
  `If` / `ElseIf` / `Else` / `EndIf`.
* A built-in `Compressor` with a soft knee and a safety limiter, which is what
  makes voice boosting usable: quiet speech comes up, loud speech does not clip.
* Config file hot-reload. Edit `config.txt`, or press Apply in the editor, and
  the change is live on the next audio block; no service restart, no reboot.
* A small editor (`ApoLiteEditor.exe`) with the voice bands, a response curve,
  and a plain-language summary of what the settings will do. No text editing
  required, and no UI framework to install.

## Performance

The audio path is allocation-free. All buffers, coefficients and FFT plans are
built when a configuration is loaded, never while audio is flowing. The audio
thread reads a single atomic pointer to the current prepared chain, so a reload
cannot block or glitch it.

Design choices that follow from that:

* No mutex is taken in `APOProcess`.
* Interleaved float input is processed in place; the in-place path does not copy.
* Integer PCM and split buffers go through one reusable scratch buffer.
* FIR work is partitioned (256-sample blocks) and primed, so convolution adds no
  latency to the conferencing round trip.

## Getting the audio you want for Zoom

Run **`ApoLiteEditor.exe`**. It opens on the voice preset, shows the response
curve, and writes the configuration when you press **Apply**. That is the whole
workflow; the rest of this section explains what the controls do.

The preset is also in `config/voice-mic.txt`. It is written for the microphone
(capture) side, so it changes what other people hear and leaves your playback
alone.

```ini
Stage: capture
Preamp: -3 dB
Filter: ON HP Fc 90 Hz
Filter: ON PK Fc 250 Hz Gain -2.5 dB Q 1.1
Filter: ON PK Fc 3000 Hz Gain 3.0 dB Q 1.0
Compressor: Threshold -22 Ratio 3 Attack 5 Release 120 Makeup 8 Knee 6 Limit -1
```

The `Compressor` line does the actual boost. `Makeup 8` raises everything, the
ratio pulls down the loud parts, and `Limit -1` guarantees the result stays
1 dB below full scale so the final conversion to integer PCM cannot clip.

## Building

Windows 11, either toolchain works.

**MSVC**

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

**LLVM / MinGW (cross-compiling from Linux is supported)**

```sh
cmake -B build -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++
cmake --build build
```

Output:

* `ApoLite.dll` - the audio processing object.
* `ApoLiteEditor.exe` - the editor. A single self-contained file.
* `apolite.exe` - a command-line tool for validating configurations offline.

## Installing

An APO must be registered from an elevated prompt. From the `build` directory:

```bat
regsvr32 ApoLite.dll
```

Then run `ApoLiteEditor.exe` and press **Apply**, which writes
`%ProgramData%\ApoLite\config\config.txt`. Open the **Sound** control panel,
select your microphone, and enable the ApoLite effect under **Enhancements**.
The change takes effect immediately.

To remove it:

```bat
regsvr32 /u ApoLite.dll
```

See [docs/install.md](docs/install.md) for the full walkthrough, including how
to confirm the DLL is loaded.

## Configuration

The language is a compatible subset of Equalizer APO's. See
[docs/configuration.md](docs/configuration.md) for every supported command, and
[docs/architecture.md](docs/architecture.md) for how the pieces fit together.

## Testing

The DSP core is platform independent and has no dependency on Windows, so the
suite runs anywhere:

```sh
cmake -B build && cmake --build build && ctest --test-dir build
```

or directly:

```sh
g++ -std=c++20 -Iinclude tests/test_apolite.cpp src/core/*.cpp -o test_apolite && ./test_apolite
```

## Compatibility notes

ApoLite deliberately targets Windows 11 only. It uses Windows 11 audio APIs and
does not carry fallbacks for Windows 7, 8 or 10. If you need those, use
Equalizer APO.

## License

MIT. Copyright (c) 2026 Joel Mandell. See [LICENSE](LICENSE).
