// ApoLite - device setup: makes the APO selectable in the Enhancements tab.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
//
// `regsvr32 ApoLite.dll` registers the COM class and calls RegisterAPO, but that
// alone never makes Windows use the APO. Windows loads an APO only when the
// audio endpoint's own key names it:
//
//   HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio
//       \(Render|Capture)\(endpoint GUID)\FxProperties
//
// This tool writes that association for the devices you choose, keeping the
// driver's original slot values so `uninstall` can restore them. Run elevated.
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "apolite/apolite.h"
#include "apolite/registry.h"

#include "device_list.h"
#include "win_registry.h"

namespace {

using apolite::registry::InstallMode;
using apolite::registry::SlotAssignment;

void printUsage() {
    std::printf(
        "ApoLite %s - device setup\n"
        "\n"
        "Makes ApoLite selectable in a device's Enhancements tab. Run elevated.\n"
        "\n"
        "Usage:\n"
        "  ApoLiteSetup list                 show devices and whether ApoLite is installed\n"
        "  ApoLiteSetup install <index|all>  attach ApoLite to device(s)\n"
        "  ApoLiteSetup uninstall <index|all> detach ApoLite and restore the driver's APOs\n"
        "  ApoLiteSetup status               report what is missing for ApoLite to load\n"
        "\n"
        "Registering the DLL (regsvr32) is a separate, required step; run it first.\n",
        APOLITE_VERSION_STRING);
}

std::wstring endpointKey(const apolite::setup::DeviceInfo& device) {
    const std::wstring tree =
        device.capture ? apolite::registry::kCaptureKey : apolite::registry::kRenderKey;
    return tree + L"\\" + device.endpointGuid;
}

std::wstring fxKey(const apolite::setup::DeviceInfo& device) {
    return endpointKey(device) + L"\\FxProperties";
}

std::wstring childApoKey(const apolite::setup::DeviceInfo& device) {
    return std::wstring(apolite::registry::kChildApoKey) + L"\\" + device.endpointGuid;
}

// True when any of the slots already names one of our CLSIDs.
bool isInstalled(const apolite::setup::DeviceInfo& device) {
    const std::wstring key = fxKey(device);
    for (size_t i = 0; i < apolite::registry::kSlotCount; ++i) {
        const auto value = apolite::setup::readValue(key, apolite::registry::kAllSlots[i]);
        if (value.kind == apolite::setup::ValueKind::String &&
            apolite::registry::isApoLiteClsid(value.stringValue)) {
            return true;
        }
    }
    return false;
}

// Copies the driver's current slot values into ApoLite's own key so uninstalling
// can put them back. Only written once, so a reinstall does not overwrite the
// saved originals with ApoLite's own CLSIDs.
void saveOriginals(const apolite::setup::DeviceInfo& device) {
    const std::wstring source = fxKey(device);
    const std::wstring destination = childApoKey(device);

    // writeString needs the key to exist; this one is ours, so it needs no
    // ownership change.
    if (!apolite::setup::createKeyWritable(destination)) return;

    for (size_t i = 0; i < apolite::registry::kSlotCount; ++i) {
        const std::wstring slot = apolite::registry::kAllSlots[i];
        if (apolite::setup::readValue(destination, slot).present()) continue;
        const auto value = apolite::setup::readValue(source, slot);
        if (value.kind == apolite::setup::ValueKind::String) {
            apolite::setup::writeString(destination, slot, value.stringValue);
        }
    }
}

// The values the driver published, which is what decides the install mode. Once
// ApoLite is installed the live key holds our own CLSIDs, so the saved originals
// are the only record of what the driver actually had; reading the live key then
// would pick a different mode and orphan the slot we already own.
bool driverSlotPresent(const apolite::setup::DeviceInfo& device, const wchar_t* slot) {
    const auto saved = apolite::setup::readValue(childApoKey(device), slot);
    if (saved.present()) {
        return saved.kind == apolite::setup::ValueKind::String && !saved.stringValue.empty();
    }
    const auto live = apolite::setup::readValue(fxKey(device), slot);
    return live.kind == apolite::setup::ValueKind::String && !live.stringValue.empty();
}

bool installDevice(const apolite::setup::DeviceInfo& device) {
    const std::wstring key = fxKey(device);
    if (!apolite::setup::createKeyWritable(key)) {
        std::wprintf(L"  %s: %s\n", device.friendlyName.c_str(),
                     apolite::setup::lastError().c_str());
        return false;
    }

    // Decide from what the driver publishes, before anything is overwritten.
    // The combined-device flag lives under Properties, not FxProperties.
    const bool combined = apolite::setup::readValue(
                              endpointKey(device) + L"\\" + apolite::registry::kPropertiesSubkey,
                              apolite::registry::kCombinedDevice)
                              .present();

    const InstallMode mode = apolite::registry::chooseInstallMode(
        driverSlotPresent(device, apolite::registry::kLfx),
        driverSlotPresent(device, apolite::registry::kGfx),
        driverSlotPresent(device, apolite::registry::kSfx),
        driverSlotPresent(device, apolite::registry::kMfx),
        driverSlotPresent(device, apolite::registry::kEfx), combined);

    saveOriginals(device);

    const auto plan = apolite::registry::slotPlan(mode, device.capture);
    const auto inPlan = [&](const std::wstring& slot) {
        for (const SlotAssignment& assignment : plan) {
            if (slot == assignment.slot) return true;
        }
        return false;
    };

    for (const SlotAssignment& assignment : plan) {
        if (!apolite::setup::writeString(key, assignment.slot,
                                         apolite::registry::clsidFor(assignment.preMix))) {
            std::wprintf(L"  %s: %s\n", device.friendlyName.c_str(),
                         apolite::setup::lastError().c_str());
            return false;
        }
        // A slot registered through the modern names is ignored without a
        // processing mode, so supply the default when the driver has none.
        if (assignment.writeModes && assignment.modes != nullptr &&
            !apolite::setup::readValue(key, assignment.modes).present()) {
            apolite::setup::writeMultiString(key, assignment.modes,
                                             apolite::registry::kDefaultMode);
        }
    }

    // A slot we own but no longer use still shadows the driver's APO, so put the
    // driver's value back. This happens when a device is reinstalled under a
    // different mode.
    for (size_t i = 0; i < apolite::registry::kSlotCount; ++i) {
        const std::wstring slot = apolite::registry::kAllSlots[i];
        if (inPlan(slot)) continue;
        const auto value = apolite::setup::readValue(key, slot);
        if (value.kind != apolite::setup::ValueKind::String ||
            !apolite::registry::isApoLiteClsid(value.stringValue)) {
            continue;
        }
        const auto original = apolite::setup::readValue(childApoKey(device), slot);
        if (original.kind == apolite::setup::ValueKind::String) {
            apolite::setup::writeString(key, slot, original.stringValue);
        } else {
            apolite::setup::deleteValue(key, slot);
        }
    }

    // A driver may have hidden the Enhancements tab; clear that for this device.
    apolite::setup::deleteValue(key, apolite::registry::kDisableEnhancements);
    // Name the FxProperties store only when it has no name yet. An existing name
    // belongs to the driver, and Windows shows it as the Enhancements entry.
    if (!apolite::setup::readValue(key, apolite::registry::kFxTitle).present()) {
        apolite::setup::writeString(key, apolite::registry::kFxTitle, L"ApoLite");
    }

    std::wprintf(L"  %s (%s): installed\n", device.friendlyName.c_str(),
                 device.capture ? L"input" : L"output");
    return true;
}

bool uninstallDevice(const apolite::setup::DeviceInfo& device) {
    const std::wstring key = fxKey(device);
    if (!apolite::setup::keyExists(key)) return true;

    const std::wstring saved = childApoKey(device);
    for (size_t i = 0; i < apolite::registry::kSlotCount; ++i) {
        const std::wstring slot = apolite::registry::kAllSlots[i];
        const auto value = apolite::setup::readValue(key, slot);
        // Only undo a slot that is actually ours; another tool may have taken it.
        if (value.kind != apolite::setup::ValueKind::String ||
            !apolite::registry::isApoLiteClsid(value.stringValue)) {
            continue;
        }
        const auto original = apolite::setup::readValue(saved, slot);
        if (original.kind == apolite::setup::ValueKind::String) {
            apolite::setup::writeString(key, slot, original.stringValue);
        } else {
            apolite::setup::deleteValue(key, slot);
        }
    }

    apolite::setup::deleteKey(saved);
    std::wprintf(L"  %s (%s): removed\n", device.friendlyName.c_str(),
                 device.capture ? L"input" : L"output");
    return true;
}

// Reports the two prerequisites that live outside the endpoint key. Without
// these the APO is associated but still never loaded.
void reportStatus() {
    std::printf("Prerequisites:\n");

    const auto protectedAudio =
        apolite::setup::readValue(apolite::registry::kAudioSettingsKey,
                                  apolite::registry::kProtectedAudioDgValue);
    const bool disabled = protectedAudio.kind == apolite::setup::ValueKind::Dword &&
                          protectedAudio.dwordValue == 1;
    std::printf("  DisableProtectedAudioDG: %s\n",
                disabled ? "set (unsigned APOs allowed)"
                         : "NOT set - run 'ApoLiteSetup install' or set it manually");

    const std::wstring postKey =
        std::wstring(apolite::registry::kAudioEngineKey) + L"\\" +
        APOLITE_POST_MIX_GUID_STRING;
    const std::wstring preKey =
        std::wstring(apolite::registry::kAudioEngineKey) + L"\\" +
        APOLITE_PRE_MIX_GUID_STRING;
    std::printf("  RegisterAPO entries: %s\n",
                apolite::setup::keyExists(postKey) && apolite::setup::keyExists(preKey)
                    ? "present"
                    : "MISSING - run 'regsvr32 ApoLite.dll' elevated");

    if (!apolite::setup::isElevated()) {
        std::printf("\nNot elevated: install and uninstall need administrator rights.\n");
    }
}

// Enables unsigned APOs for the whole machine. Required because ApoLite is not
// signed; without it the audio engine refuses to load the DLL.
bool allowUnsignedApos() {
    if (!apolite::setup::writeDword(apolite::registry::kAudioSettingsKey,
                                    apolite::registry::kProtectedAudioDgValue, 1)) {
        std::wprintf(L"Could not set DisableProtectedAudioDG: %s\n",
                     apolite::setup::lastError().c_str());
        return false;
    }
    return true;
}

void listDevices(const std::vector<apolite::setup::DeviceInfo>& devices) {
    if (devices.empty()) {
        std::printf("No audio endpoints found.\n");
        return;
    }
    std::printf("%-4s %-8s %-10s %s\n", "Idx", "Dir", "ApoLite", "Device");
    for (size_t i = 0; i < devices.size(); ++i) {
        std::wprintf(L"%-4zu %-8s %-10s %s\n", i + 1, devices[i].capture ? L"input" : L"output",
                     isInstalled(devices[i]) ? L"installed" : L"-",
                     devices[i].friendlyName.c_str());
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }
    const std::wstring command = argv[1];

    if (command == L"status") {
        reportStatus();
        return 0;
    }

    const auto devices = apolite::setup::enumerateDevices();

    if (command == L"list") {
        listDevices(devices);
        return 0;
    }

    if (command != L"install" && command != L"uninstall") {
        printUsage();
        return 1;
    }

    if (argc < 3) {
        std::printf("Specify a device index from 'list', or 'all'.\n");
        return 1;
    }

    if (!apolite::setup::isElevated()) {
        std::printf("Administrator rights are required. Re-run from an elevated prompt.\n");
        return 1;
    }

    // Unsigned APOs are refused by the audio engine unless this is set.
    if (command == L"install" && !allowUnsignedApos()) return 1;

    std::vector<size_t> targets;
    if (std::wstring(argv[2]) == L"all") {
        for (size_t i = 0; i < devices.size(); ++i) targets.push_back(i);
    } else {
        const long index = std::wcstol(argv[2], nullptr, 10);
        if (index < 1 || static_cast<size_t>(index) > devices.size()) {
            std::printf("No such device index. Run 'list' first.\n");
            return 1;
        }
        targets.push_back(static_cast<size_t>(index - 1));
    }

    int failures = 0;
    for (const size_t index : targets) {
        const bool ok = command == L"install" ? installDevice(devices[index])
                                              : uninstallDevice(devices[index]);
        if (!ok) ++failures;
    }

    if (command == L"install") {
        std::printf(
            "\nDone. Open Sound settings, choose the device, then Properties > "
            "Enhancements and tick ApoLite.\n");
    }
    return failures == 0 ? 0 : 1;
}
