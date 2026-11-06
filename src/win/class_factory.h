// ApoLite - COM class factory for the APO.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#pragma once

#include <atomic>

#include "apo_abi.h"

namespace apolite {

class ApoLiteClassFactory final : public IClassFactory {
public:
    ApoLiteClassFactory();

    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    STDMETHOD(CreateInstance)(IUnknown* pUnkOuter, REFIID riid, void** ppv) override;
    STDMETHOD(LockServer)(BOOL bLock) override;

    static long lockCount() { return lockCount_.load(); }

private:
    ~ApoLiteClassFactory() = default;

    std::atomic<ULONG> refCount_{1};
    static std::atomic<long> lockCount_;
};

}  // namespace apolite
