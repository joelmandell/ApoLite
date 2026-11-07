// ApoLite - enumerates active audio endpoints and their registry GUIDs.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#pragma once

#include <string>
#include <vector>

namespace apolite {
namespace setup {

struct DeviceInfo {
    std::wstring endpointGuid;   // {..}, the key under MMDevices\Audio\{Render|Capture}
    std::wstring friendlyName;   // what the Sound control panel shows
    bool capture = false;        // true: recording endpoint
    bool isDefault = false;
};

// Active render and capture endpoints, defaults first.
std::vector<DeviceInfo> enumerateDevices();

}  // namespace setup
}  // namespace apolite
