// ApoLite - runtime binding to audioeng.dll's APO registration API.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include <windows.h>

#include "apo_abi.h"

namespace apolite {
namespace apo {

namespace {

using RegisterApoFn = HRESULT(WINAPI*)(const APO_REG_PROPERTIES*);
using UnregisterApoFn = HRESULT(WINAPI*)(REFCLSID);

// The audio engine is already loaded in any process that instantiates an APO,
// so LoadLibraryW just bumps its reference count.
template <typename Fn>
Fn resolve(const char* name) {
    HMODULE module = LoadLibraryW(L"audioeng.dll");
    if (module == nullptr) return nullptr;
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(module, name)));
}

}  // namespace

HRESULT registerApo(const APO_REG_PROPERTIES* pProperties) {
    static const RegisterApoFn fn = resolve<RegisterApoFn>("RegisterAPO");
    if (fn == nullptr) return E_NOTIMPL;
    return fn(pProperties);
}

HRESULT unregisterApo(REFCLSID clsid) {
    static const UnregisterApoFn fn = resolve<UnregisterApoFn>("UnregisterAPO");
    if (fn == nullptr) return E_NOTIMPL;
    return fn(clsid);
}

}  // namespace apo
}  // namespace apolite
