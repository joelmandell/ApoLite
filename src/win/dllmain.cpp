// ApoLite - DLL entry points and APO registration.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include <windows.h>

#include <string>
#include <vector>

#include "apolite/apolite.h"
#include "apo.h"
#include "apo_abi.h"
#include "class_factory.h"

namespace {

HINSTANCE g_module = nullptr;

std::wstring modulePath() {
    wchar_t buffer[MAX_PATH] = {};
    GetModuleFileNameW(g_module, buffer, MAX_PATH);
    return buffer;
}

std::wstring guidToString(REFGUID guid) {
    wchar_t buffer[40] = {};
    StringFromGUID2(guid, buffer, 40);
    return buffer;
}

HRESULT registerClsid(REFGUID clsid, const wchar_t* description, const std::wstring& dllPath) {
    const std::wstring key = L"SOFTWARE\\Classes\\CLSID\\" + guidToString(clsid);
    HKEY hkey = nullptr;

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hkey,
                        nullptr) != ERROR_SUCCESS) {
        return E_FAIL;
    }
    RegSetValueExW(hkey, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(description),
                   static_cast<DWORD>((wcslen(description) + 1) * sizeof(wchar_t)));
    RegCloseKey(hkey);

    const std::wstring inproc = key + L"\\InprocServer32";
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, inproc.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr,
                        &hkey, nullptr) != ERROR_SUCCESS) {
        return E_FAIL;
    }
    RegSetValueExW(hkey, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(dllPath.c_str()),
                   static_cast<DWORD>((dllPath.size() + 1) * sizeof(wchar_t)));
    const wchar_t* threading = L"Both";
    RegSetValueExW(hkey, L"ThreadingModel", 0, REG_SZ, reinterpret_cast<const BYTE*>(threading),
                   static_cast<DWORD>((wcslen(threading) + 1) * sizeof(wchar_t)));
    RegCloseKey(hkey);
    return S_OK;
}

HRESULT unregisterClsid(REFGUID clsid) {
    const std::wstring key = L"SOFTWARE\\Classes\\CLSID\\" + guidToString(clsid);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, key.c_str());
    return S_OK;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void* /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

extern "C" STDAPI DllCanUnloadNow() {
    if (apolite::apoInstanceCount() == 0 && apolite::ApoLiteClassFactory::lockCount() == 0) {
        return S_OK;
    }
    return S_FALSE;
}

extern "C" STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    if (clsid != apolite::kPostMixClsid && clsid != apolite::kPreMixClsid) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    auto* factory = new (std::nothrow) apolite::ApoLiteClassFactory();
    if (factory == nullptr) return E_OUTOFMEMORY;

    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

extern "C" STDAPI DllRegisterServer() {
    const std::wstring dllPath = modulePath();

    // iidAPOInterfaceList is declared as IID[1] but holds u32NumAPOInterfaces
    // entries; allocate a buffer large enough for two.
    constexpr UINT32 kInterfaceCount = 2;
    std::vector<BYTE> storage(sizeof(APO_REG_PROPERTIES) +
                              (kInterfaceCount - 1) * sizeof(IID));
    APO_REG_PROPERTIES& props = *reinterpret_cast<APO_REG_PROPERTIES*>(storage.data());
    props = {};
    props.Flags = APO_FLAG_DEFAULT;
    wcscpy_s(props.szFriendlyName, L"ApoLite");
    wcscpy_s(props.szCopyrightInfo, L"MIT licensed. Copyright (c) 2026 Joel Mandell.");
    props.u32MajorVersion = apolite::kVersionMajor;
    props.u32MinorVersion = apolite::kVersionMinor;
    props.u32MinInputConnections = 1;
    props.u32MaxInputConnections = 1;
    props.u32MinOutputConnections = 1;
    props.u32MaxOutputConnections = 1;
    props.u32MaxInstances = 0;
    props.u32NumAPOInterfaces = kInterfaceCount;

    // Post-mix slot.
    props.clsid = apolite::kPostMixClsid;
    props.iidAPOInterfaceList[0] = IID_IAudioProcessingObject;
    reinterpret_cast<IID*>(storage.data() + sizeof(APO_REG_PROPERTIES))[0] =
        IID_IAudioSystemEffects2;
    HRESULT hr = apolite::apo::registerApo(&props);
    if (FAILED(hr)) return hr;
    if (FAILED(hr = registerClsid(apolite::kPostMixClsid, L"ApoLite Post-Mix Class", dllPath))) {
        apolite::apo::unregisterApo(apolite::kPostMixClsid);
        return hr;
    }

    // Pre-mix slot.
    props.clsid = apolite::kPreMixClsid;
    hr = apolite::apo::registerApo(&props);
    if (FAILED(hr)) {
        unregisterClsid(apolite::kPostMixClsid);
        apolite::apo::unregisterApo(apolite::kPostMixClsid);
        return hr;
    }
    if (FAILED(hr = registerClsid(apolite::kPreMixClsid, L"ApoLite Pre-Mix Class", dllPath))) {
        apolite::apo::unregisterApo(apolite::kPreMixClsid);
        unregisterClsid(apolite::kPostMixClsid);
        apolite::apo::unregisterApo(apolite::kPostMixClsid);
        return hr;
    }

    return S_OK;
}

extern "C" STDAPI DllUnregisterServer() {
    unregisterClsid(apolite::kPreMixClsid);
    unregisterClsid(apolite::kPostMixClsid);
    apolite::apo::unregisterApo(apolite::kPreMixClsid);
    apolite::apo::unregisterApo(apolite::kPostMixClsid);
    return S_OK;
}
