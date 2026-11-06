# Installing ApoLite

ApoLite is a Windows Audio Processing Object (APO). Windows loads it into the
audio engine process for a chosen endpoint, where it processes every audio block
in real time.

## Requirements

* Windows 11 (any edition).
* Administrator rights for registration.
* No runtime dependencies. The DLL links its C++ runtime statically.

## 1. Build

See the [README](../README.md). You end up with `ApoLite.dll`.

## 2. Register the APO

Registration writes to `HKLM`, so run an **elevated** command prompt:

```bat
regsvr32 ApoLite.dll
```

A dialog appears on success. Under the hood this calls `DllRegisterServer`,
which calls `RegisterAPO` for both the pre-mix and post-mix slots and creates the
matching `CLSID` keys.

To undo it:

```bat
regsvr32 /u ApoLite.dll
```

## 3. Install a configuration

Create the config directory and copy a preset:

```bat
mkdir "%ProgramData%\ApoLite\config"
copy config\voice-mic.txt "%ProgramData%\ApoLite\config\config.txt"
```

ApoLite watches that directory. Saving a new `config.txt` reloads the chain on
the next audio block, so there is no need to restart anything while you tune.

## 4. Enable the effect on a device

1. Open **Settings → System → Sound → More sound settings**, or run
   `mmsys.cpl`.
2. Open **Recording** for a microphone, or **Playback** for output.
3. Select the device, choose **Properties**, then the **Enhancements** tab.
4. Tick **ApoLite** and apply.

If you do not see an **Enhancements** tab, the device driver does not expose the
system effect slot. This is a driver limitation, not an ApoLite one; ApoLite is
an *effect*, not a separate virtual device.

## Verifying it is running

While audio is playing, check that the module is loaded into the audio process:

```bat
tasklist /m ApoLite.dll
```

`audiodg.exe` should appear in the list. If it does not, the effect is not
enabled on any active device.

Diagnostics are written with `OutputDebugString`, so they show up in
[DebugView](https://learn.microsoft.com/sysinternals/downloads/debugview) or any
attached debugger.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `regsvr32` reports a load error | Missing dependency, or not elevated | Run elevated; confirm the DLL is x64 |
| No **Enhancements** tab | Driver does not support system effects | Use a device that does, or a virtual audio cable |
| Effect enabled but no change | Config file not found | Confirm `%ProgramData%\ApoLite\config\config.txt` exists |
| Clicks on enable/disable | Endpoint format change | Expected once; ApoLite adds no latency of its own |

## Uninstalling

```bat
regsvr32 /u ApoLite.dll
```

Then delete `%ProgramData%\ApoLite`. Unregistering removes the `CLSID` keys and
the `RegisterAPO` entries; nothing else is written outside that folder.
