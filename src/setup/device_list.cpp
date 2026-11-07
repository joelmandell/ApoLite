// ApoLite - enumerates active audio endpoints and their registry GUIDs.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include "device_list.h"

#include <windows.h>

#include <mmdeviceapi.h>

#include <cstddef>

namespace apolite {
namespace setup {

namespace {

// Declared here rather than pulled from functiondiscoverykeys_devpkey.h, so the
// tool does not depend on a particular SDK layout.
const PROPERTYKEY kDeviceFriendlyName = {
    {0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};
const PROPERTYKEY kEndpointGuid = {
    {0x1DA5D803, 0xD492, 0x4EDD, {0x8C, 0x23, 0xE0, 0xC0, 0xFF, 0xEE, 0x7F, 0x0E}}, 4};

std::wstring stringProperty(IPropertyStore* store, const PROPERTYKEY& key) {
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring result;
    if (SUCCEEDED(store->GetValue(key, &value)) && value.vt == VT_LPWSTR &&
        value.pwszVal != nullptr) {
        result = value.pwszVal;
    }
    PropVariantClear(&value);
    return result;
}

void collect(IMMDeviceCollection* collection, bool capture, std::vector<DeviceInfo>& out) {
    if (collection == nullptr) return;
    UINT count = 0;
    if (FAILED(collection->GetCount(&count))) return;

    for (UINT i = 0; i < count; ++i) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(i, &device)) || device == nullptr) continue;

        IPropertyStore* store = nullptr;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && store != nullptr) {
            DeviceInfo info;
            info.capture = capture;
            info.friendlyName = stringProperty(store, kDeviceFriendlyName);
            info.endpointGuid = stringProperty(store, kEndpointGuid);
            store->Release();
            // Without the endpoint GUID there is no registry key to write.
            if (!info.endpointGuid.empty()) out.push_back(std::move(info));
        }
        device->Release();
    }
}

}  // namespace

std::vector<DeviceInfo> enumerateDevices() {
    // The COM apartment may already exist; either mode works for this call.
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(initialized);

    std::vector<DeviceInfo> devices;

    IMMDeviceEnumerator* enumerator = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                   __uuidof(IMMDeviceEnumerator),
                                   reinterpret_cast<void**>(&enumerator))) &&
        enumerator != nullptr) {
        // Mark the defaults so 'list' can show what is most likely wanted.
        std::vector<std::wstring> defaults;
        for (const ERole role : {eConsole, eMultimedia, eCommunications}) {
            for (const bool capture : {false, true}) {
                IMMDevice* device = nullptr;
                if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(capture ? eCapture : eRender,
                                                                  role, &device)) &&
                    device != nullptr) {
                    IPropertyStore* store = nullptr;
                    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) &&
                        store != nullptr) {
                        defaults.push_back(stringProperty(store, kEndpointGuid));
                        store->Release();
                    }
                    device->Release();
                }
            }
        }

        IMMDeviceCollection* collection = nullptr;
        if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection))) {
            collect(collection, /*capture=*/false, devices);
            collection->Release();
        }
        if (SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection))) {
            collect(collection, /*capture=*/true, devices);
            collection->Release();
        }
        enumerator->Release();

        for (DeviceInfo& device : devices) {
            for (const std::wstring& guid : defaults) {
                if (device.endpointGuid == guid) device.isDefault = true;
            }
        }
    }

    if (uninitialize) CoUninitialize();
    return devices;
}

}  // namespace setup
}  // namespace apolite
