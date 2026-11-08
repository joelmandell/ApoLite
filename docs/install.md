# Installing ApoLite

ApoLite is a Windows Audio Processing Object (APO). Windows loads it into the
audio engine process for a chosen endpoint, where it processes every audio block
in real time.

## Requirements

* Windows 11 (any edition).
* Administrator rights for registration.
* No runtime dependencies. The DLL links its C++ runtime statically.

## 1. Build

See the [README](../README.md). You end up with `ApoLite.dll` and
`ApoLiteEditor.exe`.

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

## 3. Attach the APO to a device

This step is required, and `regsvr32` does **not** do it. Windows loads an APO
only when the audio endpoint's own registry key names it:

```
HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio
    \(Render|Capture)\(endpoint GUID)\FxProperties
```

Without it the APO is registered but never loaded, so it never appears in the
**Enhancements** tab. Run the setup tool, elevated, from the folder holding
`ApoLite.dll`:

```bat
ApoLiteSetup list                 :: devices, and whether ApoLite is attached
ApoLiteSetup install 1            :: attach to device 1 (or "all")
ApoLiteSetup status               :: check the two machine-wide prerequisites
ApoLiteSetup uninstall 1          :: detach and restore the driver's own APOs
```

`install` also sets `DisableProtectedAudioDG`, which the audio engine requires
before it will load an unsigned APO. That setting is machine-wide and weakens the
protected audio path; see the note below.

## 4. Install a configuration

The easiest way is to run **`ApoLiteEditor.exe`** and press **Apply**. It writes
`%ProgramData%\ApoLite\config\config.txt` for you. A second launch just
raises the window that is already open.

If you would rather not use the editor, copy the preset by hand:

```bat
mkdir "%ProgramData%\ApoLite\config"
copy config\voice-mic.txt "%ProgramData%\ApoLite\config\config.txt"
```

Either way ApoLite watches that directory. Saving a new `config.txt` reloads the
chain on the next audio block, so there is no need to restart anything while you
tune. The editor and the file are interchangeable: the editor reads whatever is
in the file, including changes you made by hand.

## 5. Enable the effect on a device

1. Open **Settings → System → Sound → More sound settings**, or run
   `mmsys.cpl`.
2. Open **Recording** for a microphone, or **Playback** for output.
3. Select the device, choose **Properties**, then the **Enhancements** tab.
4. Tick **ApoLite** and apply.

If you do not see an **Enhancements** tab, the device driver does not expose the
system effect slot. This is a driver limitation, not an ApoLite one; ApoLite is
an *effect*, not a separate virtual device. Use `ApoLiteSetup list` to see which
endpoints Windows reports.

### Note on the driver's own effects

A device slot holds one APO. Attaching ApoLite replaces whatever the driver had
there, and the original value is saved under
`HKLM\SOFTWARE\ApoLite\Child APOs` so `uninstall` can put it back.

ApoLite does **not** call the replaced APO. Equalizer APO chains to it so the
driver's own effects keep working; ApoLite does not, so the driver's effects on
that slot are inactive while ApoLite is attached. On the microphone path this is
usually wanted. `ApoLiteSetup uninstall` restores the original exactly.

## Verifying it is running

While audio is playing, check that the module is loaded into the audio process:

```bat
tasklist /m ApoLite.dll
```

`audiodg.exe` should appear in the list. If it does not, the effect is not
enabled on any active device.

Diagnostics are written to `%ProgramData%\ApoLite\config\apolite.log`, next to
the configuration file, and to `OutputDebugString` so they also show up in
[DebugView](https://learn.microsoft.com/sysinternals/downloads/debugview) or any
attached debugger. The log is the place to look when a filter does not seem to
take effect: a filter that is dropped, such as an impulse response that does not
fit the memory budget, is reported there with the reason.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `regsvr32` reports a load error | Missing dependency, or not elevated | Run elevated; confirm the DLL is x64 |
| ApoLite missing from **Enhancements** | The APO is not attached to the endpoint | Run `ApoLiteSetup install <index>` elevated |
| ApoLite missing from **Enhancements** after that | `DisableProtectedAudioDG` not set | Run `ApoLiteSetup status`; `install` sets it |
| No **Enhancements** tab at all | Driver does not support system effects | Use a device that does, or a virtual audio cable |
| Effect enabled but no change | Config file not found | Confirm `%ProgramData%\ApoLite\config\config.txt` exists |
| Apply fails with a write error | The config folder needs administrator rights | Run the editor as administrator |
| Clicks on enable/disable | Endpoint format change | Expected once; ApoLite adds no latency of its own |
| Driver's own effects stopped | ApoLite replaced the driver's APO in that slot | Expected; `ApoLiteSetup uninstall` restores it |

## Uninstalling

```bat
ApoLiteSetup uninstall all
regsvr32 /u ApoLite.dll
```

Then delete `%ProgramData%\ApoLite`. Unregistering removes the `CLSID` keys and
the `RegisterAPO` entries; `ApoLiteSetup uninstall` restores each device's
original APOs and removes `HKLM\SOFTWARE\ApoLite`. Nothing else is written
outside that folder, except `DisableProtectedAudioDG`, which is shared with any
other unsigned APO and is left alone.
