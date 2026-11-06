// ApoLite - COM class factory for the APO.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include "class_factory.h"

#include "apo.h"

namespace apolite {

std::atomic<long> ApoLiteClassFactory::lockCount_{0};

ApoLiteClassFactory::ApoLiteClassFactory() = default;

STDMETHODIMP ApoLiteClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IClassFactory)) {
        *ppv = static_cast<IClassFactory*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) ApoLiteClassFactory::AddRef() {
    return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
}

STDMETHODIMP_(ULONG) ApoLiteClassFactory::Release() {
    const ULONG remaining = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0) delete this;
    return remaining;
}

STDMETHODIMP ApoLiteClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;
    if (pUnkOuter != nullptr) return CLASS_E_NOAGGREGATION;

    auto* apo = new (std::nothrow) ApoLiteApo();
    if (apo == nullptr) return E_OUTOFMEMORY;

    const HRESULT hr = apo->QueryInterface(riid, ppv);
    apo->Release();
    return hr;
}

STDMETHODIMP ApoLiteClassFactory::LockServer(BOOL bLock) {
    if (bLock) {
        lockCount_.fetch_add(1, std::memory_order_relaxed);
    } else {
        lockCount_.fetch_sub(1, std::memory_order_relaxed);
    }
    return S_OK;
}

}  // namespace apolite
