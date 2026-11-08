# Configuration

Most people never need this page: the **ApoLite editor** covers the voice bands
and writes the file for you. Read on to go beyond what the editor exposes, or to
script a configuration.

ApoLite reads a plain text file, one command per line. Blank lines and lines
starting with `#` are ignored. The syntax is a compatible subset of
[Equalizer APO](https://sourceforge.net/projects/equalizerapo/)'s, so existing
configs load with only the unsupported commands dropped.

The default location is `%ProgramData%\ApoLite\config\config.txt`. The file is
watched and hot-reloaded; saving it applies the change on the next audio block.

The editor and the file stay in sync in both directions. Anything the editor
does not understand (see [what the editor covers](#what-the-editor-covers)) is
left alone: it is shown as kept, and written back unchanged when you press Apply.

## What the editor covers

The editor writes these, and only these. Everything else in this document is for
hand editing.

| Control | Written as |
|---|---|
| Applies to | `Stage: capture` or `Stage: post-mix` |
| Output trim | `Preamp` |
| Remove rumble | `Filter: ON HP Fc ...` |
| Reduce muddiness | `Filter: ON PK Fc 250 Hz ...` |
| Voice presence | `Filter: ON PK Fc 3000 Hz ...` |
| Clarity | `Filter: ON PK Fc 6000 Hz ...` |
| Tame harshness | `Filter: ON PK Fc 9000 Hz ...` |
| Remove hiss | `Filter: ON LP Fc ...` |
| Even out loud and quiet | `Compressor` |

If you add anything else by hand, the editor keeps those lines and re-emits them
on Apply, so mixing the two is safe.

## Selecting what to process

### `Stage`

```ini
Stage: capture
```

Selects which pipeline slot this config applies to. Values are `capture`
(microphone), `pre-mix` (before the system mixer) and `post-mix` (after it). If
the line is omitted, every stage matches.

### `Device`

```ini
Device: Microphone (USB Audio)
```

Only lines after a matching `Device` are applied, until the next `Device` line.
The pattern is matched against the device name, connection name and endpoint
GUID. Use `Device: *` or omit it to match everything.

### `Channel`

```ini
Channel: L R
Channel: all
```

Sets which channels subsequent filters apply to. Accepts `L`, `R`, `C`, `LFE`,
`SL`, `SR`, `BL`, `BR`, or `all`. New channels can be named to create them via
`Copy`.

## Gain

### `Preamp`

```ini
Preamp: -6 dB
```

Constant gain applied to the selected channels. Use it to reserve headroom
before boosting.

## Filters

```ini
Filter: ON <type> [Fc <Hz>] [Gain <dB>] [Q <q> | BW <oct> | S <slope>]
```

The `ON` token is required. Types:

| Type | Meaning |
|---|---|
| `PK` | peaking bell |
| `LP`, `HP` | second-order low / high pass |
| `LPQ`, `HPQ` | low / high pass with an explicit Q |
| `BP` | band pass |
| `LS`, `LSC` | low shelf (`LSC` treats `Fc` as the corner frequency) |
| `HS`, `HSC` | high shelf (`HSC` treats `Fc` as the corner frequency) |
| `NO` | notch |
| `AP` | all pass |
| `IIR` | raw coefficients (see below) |

`Q` is the default shape parameter. `BW` is bandwidth in octaves and `S` is a
shelf slope; both are accepted for shelf and peaking filters.

Arbitrary biquads use raw coefficients, `b0 b1 b2 a0 a1 a2` for order 1:

```ini
Filter: ON IIR Order 1 Coefficients 0.5 0.5 1 -0.5
```

## GraphicEQ

```ini
GraphicEQ: 20 3; 100 0; 1000 -2; 10000 1.5
```

Pairs of `frequency gain`. ApoLite interpolates between the points and designs a
minimum-phase FIR, so phase stays consistent with the magnitude curve.

## Convolution

```ini
Convolution: room.wav
```

Convolves with an impulse response. Relative paths resolve against the directory
of the including config file.

The impulse response may be at most 262144 taps (about 5.5 seconds at 48 kHz).
A longer file is rejected with a message in the log and the rest of the chain
still loads; the limit exists so a mistaken or hostile file cannot exhaust
memory. When one impulse applies to several channels it is expanded into spectra
once and shared, so a stereo impulse does not cost twice a mono one.

## Delay

```ini
Delay: 5 ms
Delay: 240 samples
```

Adds an integer delay. A trailing `ms` selects milliseconds.

## Copy

```ini
Copy: L=R
Copy: R=L+3
```

Replaces a target channel with a weighted sum of sources. A bare number is a
constant, so `R=L+3` is the left channel plus a constant offset.

## Compressor

```ini
Compressor: Threshold -22 Ratio 3 Attack 5 Release 120 Makeup 8 Knee 6 Limit -1
```

A feed-forward compressor with a soft knee and a safety limiter. Every parameter
is optional; the defaults are tuned for speech.

| Parameter | Default | Meaning |
|---|---|---|
| `Threshold` | `-24` | level in dB where gain reduction starts |
| `Ratio` | `3` | compression ratio; large values approach limiting |
| `Attack` | `5` | attack time in ms |
| `Release` | `120` | release time in ms |
| `Makeup` | `0` | static gain applied after compression, in dB |
| `Knee` | `6` | soft-knee width in dB, centred on the threshold |
| `Limit` | `-1` | output ceiling in dB, enforced on every sample |

For raising a quiet voice without clipping, a high `Makeup` with a moderate
`Ratio` and a hard `Limit` is the combination that works.

## Control flow

### `Include`

```ini
Include: bass-boost.txt
```

Splices another file in. Relative paths resolve against the including file.
Channel, device and `If` state inside the included file do not leak back out.

### `If` / `ElseIf` / `Else` / `EndIf`

```ini
If: stage == "capture"
  Preamp: -3 dB
ElseIf: stage == "post-mix"
  Preamp: 0 dB
Else
  Preamp: -1 dB
EndIf
```

Conditions may use the variables `stage`, `device`, `channel` and `sampleRate`.
Expressions support comparison, arithmetic and boolean operators.

### Inline expressions

Backticks evaluate an expression in place:

```ini
Preamp: `-6 + 1` dB
```

## Unsupported commands

Anything not listed here is ignored rather than treated as an error. In
particular the Equalizer APO editor commands, VST plugins and the device-test
pipe have no ApoLite equivalent.
