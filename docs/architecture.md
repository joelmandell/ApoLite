# ApoLite architecture

Three layers, one direction of dependency: the Windows shell depends on the
engine, the engine depends on the DSP core, and the DSP core depends on nothing
but the C++ standard library.

```
        ApoLite.dll (src/win)          ApoLiteEditor.exe (src/gui)
        APO shell, COM, registration   Win32 window, curve, file I/O
        config watching                        |
                    |                          |  writes config.txt
                    v                          v
        Engine (src/core/engine.cpp) <---- settings.cpp
        deinterleave, run the chain,      render / parse the
        reinterleave                      configuration text
                    |
                    v
        DSP core (src/core/*, include/apolite/*)
        biquads, FFT, FIR, filters, config parser
```

The editor and the APO never talk to each other directly. They meet at
`config.txt`: the editor renders its controls to that text, and the APO watches
the file and reloads. `settings.cpp` holds the single description of what the
editor can express, and both directions (render and parse) live there, so a file
written by hand still opens correctly in the editor. Anything the editor cannot
express is reported as unsupported and written back untouched, so the two ways of
editing do not fight.

## The audio thread contract

The only function the audio engine calls on the hot path is
`Engine::processInterleaved`. Everything else happens off it.

`Engine::loadConfiguration` builds a complete `PreparedChain` - the filter
objects, their per-channel state, and the scratch buffers - on a control thread.
When it is finished it publishes it by storing one atomic pointer:

```cpp
published_.store(prepared.get(), std::memory_order_release);
```

`processInterleaved` reads that pointer once. There is no lock, no allocation and
no exception on the audio path. A reload during playback therefore swaps the
chain between two audio blocks instead of blocking.

The previous chain is kept alive by a `shared_ptr` until the next reload, so a
block that is mid-flight can never observe freed memory.

## Buffers

The engine works on planar (deinterleaved) `float` buffers. Interleaved input is
split once into `scratch_`, the chain runs over per-channel pointers, and the
result is interleaved back.

Filter entries carry an input list and an output list of channel indices. For
most filters the two are the same and the filter writes in place. `Copy` is the
exception: it reads every channel and writes its targets, which is why the chain
has separate `chainInPtrs_` and `chainOutPtrs_`. Those were previously one array,
which aliased for `Copy` and produced wrong output.

## Latency

The convolvers (`GraphicEQ`, `Convolution`) run a partitioned overlap-save
algorithm with 256-sample blocks. A naive implementation of that scheme delays
output by one block, which would add about 5 ms at 48 kHz to every call.

ApoLite primes each convolver with a block of silence when it is prepared, so the
first real block produces the correct output immediately. Measured latency is
zero samples.

The host may hand the engine any block size, and Windows commonly delivers 480
frames (10 ms at 48 kHz), which is not a multiple of the 256-sample convolver
block. A partial block is transformed against a zero-padded tail without
committing it to the delay line, so output for the samples that are present is
final and no latency is added. An earlier version advanced in whole blocks only,
so a partial block looped forever and hung the audio thread.

## Dynamics

`CompressorFilter` is a feed-forward peak compressor. A peak detector with
separate attack and release coefficients produces an envelope, the envelope is
converted to dB, and a soft knee spreads the gain reduction over
`threshold ± knee/2`. A final limiter clamps the peak to `limitDb`, so the
signal can never exceed the ceiling regardless of how hard it is driven.

This is what makes the microphone preset work. Makeup gain raises quiet speech;
the ratio pulls down loud speech; the limiter guarantees no clipping.

## Configuration parsing

`ConfigLoader` mirrors Equalizer APO's semantics where it matters:

* `Device:` filters lines by matching the device string.
* `Stage:` selects `capture`, `pre-mix` or `post-mix`.
* `Channel:` selects which channels later filters apply to.
* `Include:` nests, and its channel/device/if state does not leak back out.
* `If` / `ElseIf` / `Else` / `EndIf` are evaluated with the same expression
  syntax, including `` `...` `` inline expressions.

Unknown commands are ignored rather than treated as errors, which keeps existing
Equalizer APO configs loadable.

## Why the vendored ABI header

The Windows SDK header `audioenginebaseapo.h` declares the APO interfaces, but
mingw-w64 does not ship it. `src/win/apo_abi.h` declares only the pieces ApoLite
needs - the interfaces, `APO_CONNECTION_PROPERTY`, `APO_REG_PROPERTIES`,
`APOInitSystemEffects` - with layouts and IIDs matching the SDK exactly.

`RegisterAPO` and `UnregisterAPO` live in `audioeng.dll` and have no import
library in mingw. `src/win/apo_registration.cpp` resolves them with
`GetProcAddress` at first use, so the DLL links with either toolchain and still
registers correctly.
