// ApoLite - Windows audio endpoint registry layout for APO registration.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
//
// Registering a COM class and calling RegisterAPO is not enough to make Windows
// use an APO. Windows only loads an APO that is named in the audio endpoint's
// own registry key:
//
//   HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio
//       (Render|Capture)\(endpoint GUID)\FxProperties
//
// So the installer has to write that key for each device. The value names below
// are Windows' own property keys; they are fixed by the OS, not by ApoLite.
// This header is deliberately free of <windows.h> so the layout logic can be
// tested on any platform; only the tool that applies it is Windows-only.
#pragma once

#include <string>
#include <vector>

namespace apolite {
namespace registry {

// Property keys under an endpoint's FxProperties key, in Windows' own format.
// The trailing number is the property id.
inline constexpr const wchar_t* kLfx = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},1";
inline constexpr const wchar_t* kGfx = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2";
inline constexpr const wchar_t* kSfx = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},5";
inline constexpr const wchar_t* kMfx = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},6";
inline constexpr const wchar_t* kEfx = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},7";

// Processing modes. A slot registered through SFX/MFX/EFX needs one of these,
// and Windows ignores the slot when it is missing.
inline constexpr const wchar_t* kSfxModes = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},5";
inline constexpr const wchar_t* kMfxModes = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},6";
inline constexpr const wchar_t* kEfxModes = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},7";

// The one processing mode Windows ships as the default.
inline constexpr const wchar_t* kDefaultMode = L"{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}";

// Set (to 1) by a driver to hide the Enhancements tab entirely. ApoLite clears
// it for the devices it is installed to.
inline constexpr const wchar_t* kDisableEnhancements = L"{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5";

// The FxProperties display name, shown by some driver control panels.
inline constexpr const wchar_t* kFxTitle = L"{b725f130-47ef-101a-a5f1-02608c9eebac},10";

// Present on endpoints that Windows has combined (for example Bluetooth
// headset + hands-free). Such an endpoint cannot host an EFX APO. This one lives
// under the endpoint's Properties subkey, not under FxProperties.
inline constexpr const wchar_t* kPropertiesSubkey = L"Properties";
inline constexpr const wchar_t* kCombinedDevice = L"{b3f8fa53-0004-438e-9003-51a46e139bfc},41";

// Every slot ApoLite may need to read or restore.
inline constexpr const wchar_t* kAllSlots[] = {kLfx, kGfx, kSfx, kMfx, kEfx};
inline constexpr size_t kSlotCount = 5;

// Where the APO COM class is registered (HKLM\SOFTWARE\Classes).
inline constexpr const wchar_t* kClassesRoot = L"SOFTWARE\\Classes";
inline constexpr const wchar_t* kAudioEngineKey =
    L"SOFTWARE\\Classes\\AudioEngine\\AudioProcessingObjects";
inline constexpr const wchar_t* kAudioSettingsKey =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio";
inline constexpr const wchar_t* kProtectedAudioDgValue = L"DisableProtectedAudioDG";
inline constexpr const wchar_t* kMmDevicesAudioKey =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio";
inline constexpr const wchar_t* kRenderKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Render";
inline constexpr const wchar_t* kCaptureKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Capture";

// Where ApoLite keeps the driver's original slot values so uninstalling can put
// them back. Mirrors what Equalizer APO calls "Child APOs".
inline constexpr const wchar_t* kChildApoKey = L"SOFTWARE\\ApoLite\\Child APOs";

// Which set of slots to write. Windows 8.1+ (so all of Windows 11) supports the
// newer SFX/MFX/EFX names; a driver that supplies only LFX/GFX is driven through
// those instead. When both are written the newer ones win, so a driver using the
// old names would be ignored.
enum class InstallMode {
    LfxGfx,  // driver publishes only the legacy LFX/GFX slots
    SfxMfx,  // modern slots; EFX unavailable (combined endpoint)
    SfxEfx,  // modern slots, pre-mix SFX + post-mix EFX
};

// One slot ApoLite wants to own, plus the driver's original value for it.
struct SlotAssignment {
    const wchar_t* slot = nullptr;      // value name under FxProperties
    const wchar_t* modes = nullptr;     // matching processing-modes value, or null
    bool preMix = false;                // true: our pre-mix CLSID, false: post-mix
    bool writeModes = false;            // write the default mode if absent
};

// True when the endpoint lives under the Capture tree.
inline bool isCaptureEndpoint(const std::wstring& tree) { return tree == kCaptureKey; }

// Pick the slot set for a device from what its driver already publishes.
//
// `hasLfx`/`hasGfx` are the legacy slots, `hasSfx`/`hasMfx`/`hasEfx` the modern
// ones. `combinedDevice` marks an endpoint that cannot host an EFX APO. This
// follows the rules Equalizer APO derived for Windows 8.1 and later.
InstallMode chooseInstallMode(bool hasLfx, bool hasGfx, bool hasSfx, bool hasMfx, bool hasEfx,
                              bool combinedDevice);

// The slots to write for `mode`. A capture endpoint has no post-mix stage, so
// only its pre-mix slot is returned. The result is ordered pre-mix first.
std::vector<SlotAssignment> slotPlan(InstallMode mode, bool capture);

// The ApoLite CLSID string for a slot.
const wchar_t* clsidFor(bool preMix);

// True when `value` names one of ApoLite's own CLSIDs, in any letter case.
bool isApoLiteClsid(const std::wstring& value);

}  // namespace registry
}  // namespace apolite
