// ApoLite - Windows audio endpoint registry layout for APO registration.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include "apolite/registry.h"

#include <cwctype>

#include "apolite/apolite.h"

namespace apolite {
namespace registry {

namespace {

std::wstring upper(std::wstring text) {
    for (wchar_t& c : text) c = static_cast<wchar_t>(std::towupper(c));
    return text;
}

}  // namespace

InstallMode chooseInstallMode(bool hasLfx, bool hasGfx, bool hasSfx, bool hasMfx, bool hasEfx,
                              bool combinedDevice) {
    // A driver that publishes only the legacy slots cannot be driven through the
    // modern ones: Windows would then ignore its own APOs. Only take this path
    // when the legacy slots are present and every modern one is absent.
    const bool legacyOnly = (hasLfx || hasGfx) && !hasSfx && !hasMfx && !hasEfx;
    if (legacyOnly) return InstallMode::LfxGfx;

    // A combined endpoint (Bluetooth) rejects EFX, so fall back to MFX.
    if (combinedDevice) return InstallMode::SfxMfx;

    return InstallMode::SfxEfx;
}

std::vector<SlotAssignment> slotPlan(InstallMode mode, bool capture) {
    std::vector<SlotAssignment> plan;

    switch (mode) {
        case InstallMode::LfxGfx:
            plan.push_back({kLfx, nullptr, true, false});
            if (!capture) plan.push_back({kGfx, nullptr, false, false});
            break;

        case InstallMode::SfxMfx:
            // SFX/MFX need a processing mode or Windows ignores the slot.
            plan.push_back({kSfx, kSfxModes, true, true});
            if (!capture) plan.push_back({kMfx, kMfxModes, false, true});
            break;

        case InstallMode::SfxEfx:
            plan.push_back({kSfx, kSfxModes, true, true});
            if (!capture) plan.push_back({kEfx, kEfxModes, false, true});
            break;
    }

    return plan;
}

const wchar_t* clsidFor(bool preMix) {
    return preMix ? APOLITE_PRE_MIX_GUID_STRING : APOLITE_POST_MIX_GUID_STRING;
}

bool isApoLiteClsid(const std::wstring& value) {
    const std::wstring upperValue = upper(value);
    return upperValue == APOLITE_PRE_MIX_GUID_STRING ||
           upperValue == APOLITE_POST_MIX_GUID_STRING;
}

}  // namespace registry
}  // namespace apolite
