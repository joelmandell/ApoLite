// ApoLite - minimal Windows APO ABI declarations.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
//
// The Windows SDK header <audioenginebaseapo.h> is not shipped by mingw-w64, so
// this file declares only the interfaces and structures ApoLite needs. The
// layouts and IIDs match the Windows SDK exactly.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <objbase.h>
#include <propidl.h>
#include <mmdeviceapi.h>

// Forward declaration: only used as a pointer in the structures below.
struct IAudioMediaType;

// The audio engine timespan type; normally from audioapotypes.h.
using HNSTIME = LONGLONG;

namespace apolite {
namespace apo {

// ---------------------------------------------------------------- constants

constexpr UINT32 kAudioMinChannels = 1;
constexpr UINT32 kAudioMaxChannels = 4096;

enum APO_CONNECTION_BUFFER_TYPE {
    APO_CONNECTION_BUFFER_TYPE_ALLOCATED = 0,
    APO_CONNECTION_BUFFER_TYPE_EXTERNAL = 1,
    APO_CONNECTION_BUFFER_TYPE_DEPENDANT = 2,
};

enum APO_FLAG {
    APO_FLAG_NONE = 0,
    APO_FLAG_INPLACE = 0x1,
    APO_FLAG_SAMPLESPERFRAME_MUST_MATCH = 0x2,
    APO_FLAG_FRAMESPERSECOND_MUST_MATCH = 0x4,
    APO_FLAG_BITSPERSAMPLE_MUST_MATCH = 0x8,
    APO_FLAG_MIXER = 0x10,
    APO_FLAG_DEFAULT = (APO_FLAG_SAMPLESPERFRAME_MUST_MATCH | APO_FLAG_FRAMESPERSECOND_MUST_MATCH |
                        APO_FLAG_BITSPERSAMPLE_MUST_MATCH),
};

enum APO_BUFFER_FLAGS {
    APO_BUFFER_VALID = 0,
    APO_BUFFER_SILENT = 1,
};

// APO-specific HRESULTs. They are plain HRESULT values, not an enum, so that
// static_cast<HRESULT>(...) works the same way as in the Windows SDK.
constexpr HRESULT kApoErrAlreadyInitialized = static_cast<HRESULT>(0x887D0001L);
constexpr HRESULT kApoErrNotInitialized = static_cast<HRESULT>(0x887D0002L);
constexpr HRESULT kApoErrFormatNotSupported = static_cast<HRESULT>(0x887D0003L);
constexpr HRESULT kApoErrInvalidApoClsid = static_cast<HRESULT>(0x887D0004L);
constexpr HRESULT kApoErrBuffersOverlap = static_cast<HRESULT>(0x887D0005L);
constexpr HRESULT kApoErrAlreadyUnlocked = static_cast<HRESULT>(0x887D0006L);
constexpr HRESULT kApoErrNumConnectionsInvalid = static_cast<HRESULT>(0x887D0007L);
constexpr HRESULT kApoErrInvalidOutputMaxFrameCount = static_cast<HRESULT>(0x887D0008L);
constexpr HRESULT kApoErrInvalidConnectionFormat = static_cast<HRESULT>(0x887D0009L);
constexpr HRESULT kApoErrApoLocked = static_cast<HRESULT>(0x887D000AL);

// --------------------------------------------------------------- structures

struct APO_CONNECTION_DESCRIPTOR {
    APO_CONNECTION_BUFFER_TYPE Type;
    UINT_PTR pBuffer;
    UINT32 u32MaxFrameCount;
    IAudioMediaType* pFormat;
    UINT32 u32Signature;
};

struct APO_CONNECTION_PROPERTY {
    UINT_PTR pBuffer;
    UINT32 u32ValidFrameCount;
    APO_BUFFER_FLAGS u32BufferFlags;
    UINT32 u32Signature;
};

struct APO_REG_PROPERTIES {
    CLSID clsid;
    APO_FLAG Flags;
    WCHAR szFriendlyName[256];
    WCHAR szCopyrightInfo[256];
    UINT32 u32MajorVersion;
    UINT32 u32MinorVersion;
    UINT32 u32MinInputConnections;
    UINT32 u32MaxInputConnections;
    UINT32 u32MinOutputConnections;
    UINT32 u32MaxOutputConnections;
    UINT32 u32MaxInstances;
    UINT32 u32NumAPOInterfaces;
    IID iidAPOInterfaceList[1];
};

struct APOInitBaseStruct {
    UINT32 cbSize;
    CLSID clsid;
};

struct APOInitSystemEffects {
    APOInitBaseStruct APOInit;
    IPropertyStore* pAPOEndpointProperties;
    IPropertyStore* pAPOSystemEffectsProperties;
    void* pReserved;
    IMMDeviceCollection* pDeviceCollection;
};

struct APOInitSystemEffects2 {
    APOInitBaseStruct APOInit;
    IPropertyStore* pAPOEndpointProperties;
    IPropertyStore* pAPOSystemEffectsProperties;
    void* pReserved;
    IMMDeviceCollection* pDeviceCollection;
    UINT nSoftwareIoDeviceInCollection;
    UINT nSoftwareIoConnectorIndex;
    GUID AudioProcessingMode;
    BOOL InitializeForDiscoveryOnly;
};

// ------------------------------------------------------------------ IIDs

// {9E1D6A6D-DDBC-4E95-A4C7-AD64BA37846C}
EXTERN_C const IID IID_IAudioProcessingObjectRT;
// {7BA1DB8F-78AD-49CD-9591-F79D80A17C81}
EXTERN_C const IID IID_IAudioProcessingObjectVBR;
// {0E5ED805-ABA6-49C3-8F9A-2B8C889C4FA8}
EXTERN_C const IID IID_IAudioProcessingObjectConfiguration;
// {FD7F2B29-24D0-4B5C-B177-592C39F9CA10}
EXTERN_C const IID IID_IAudioProcessingObject;
// {5FA00F27-ADD6-499A-8A9D-6B98521FA75B}
EXTERN_C const IID IID_IAudioSystemEffects;
// {BAFE99D2-7436-44CE-9E0E-4D89AFBFFF56}
EXTERN_C const IID IID_IAudioSystemEffects2;

// --------------------------------------------------------------- interfaces

MIDL_INTERFACE("9E1D6A6D-DDBC-4E95-A4C7-AD64BA37846C")
IAudioProcessingObjectRT : public IUnknown {
   public:
    virtual void STDMETHODCALLTYPE APOProcess(UINT32 u32NumInputConnections,
                                               APO_CONNECTION_PROPERTY** ppInputConnections,
                                               UINT32 u32NumOutputConnections,
                                               APO_CONNECTION_PROPERTY** ppOutputConnections) = 0;
    virtual UINT32 STDMETHODCALLTYPE CalcInputFrames(UINT32 u32OutputFrameCount) = 0;
    virtual UINT32 STDMETHODCALLTYPE CalcOutputFrames(UINT32 u32InputFrameCount) = 0;
};

MIDL_INTERFACE("7BA1DB8F-78AD-49CD-9591-F79D80A17C81")
IAudioProcessingObjectVBR : public IUnknown {
   public:
    virtual HRESULT STDMETHODCALLTYPE CalcMaxInputFrames(UINT32 u32MaxOutputFrameCount,
                                                         UINT32* pu32InputFrameCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE CalcMaxOutputFrames(UINT32 u32MaxInputFrameCount,
                                                          UINT32* pu32OutputFrameCount) = 0;
};

MIDL_INTERFACE("0E5ED805-ABA6-49C3-8F9A-2B8C889C4FA8")
IAudioProcessingObjectConfiguration : public IUnknown {
   public:
    virtual HRESULT STDMETHODCALLTYPE LockForProcess(
        UINT32 u32NumInputConnections, APO_CONNECTION_DESCRIPTOR** ppInputConnections,
        UINT32 u32NumOutputConnections, APO_CONNECTION_DESCRIPTOR** ppOutputConnections) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnlockForProcess(void) = 0;
};

MIDL_INTERFACE("FD7F2B29-24D0-4B5C-B177-592C39F9CA10")
IAudioProcessingObject : public IUnknown {
   public:
    virtual HRESULT STDMETHODCALLTYPE Initialize(UINT32 cbDataSize, BYTE* pbyData) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLatency(HNSTIME* pTime) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRegistrationProperties(APO_REG_PROPERTIES** ppRegProps) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsInputFormatSupported(IAudioMediaType* pOppositeFormat,
                                                             IAudioMediaType* pRequestedInputFormat,
                                                             IAudioMediaType** ppSupportedInputFormat) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(IAudioMediaType* pOppositeFormat,
                                                              IAudioMediaType* pRequestedOutputFormat,
                                                              IAudioMediaType** ppSupportedOutputFormat) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetInputChannelCount(UINT32* pu32ChannelCount) = 0;
};

MIDL_INTERFACE("5FA00F27-ADD6-499A-8A9D-6B98521FA75B")
IAudioSystemEffects : public IUnknown {
   public:
};

MIDL_INTERFACE("BAFE99D2-7436-44CE-9E0E-4D89AFBFFF56")
IAudioSystemEffects2 : public IAudioSystemEffects {
   public:
    virtual HRESULT STDMETHODCALLTYPE GetEffectsList(LPGUID* ppEffectsIds, UINT* pcEffects,
                                                     HANDLE Event) = 0;
};

// ------------------------------------------------------- registration APIs

// RegisterAPO/UnregisterAPO live in audioeng.dll. They are resolved at runtime
// so the DLL links with any toolchain (the Windows SDK import library is not
// required).
HRESULT registerApo(const APO_REG_PROPERTIES* pProperties);
HRESULT unregisterApo(REFCLSID clsid);

}  // namespace apo
}  // namespace apolite

// The audio engine looks these up by their plain names.
using apolite::apo::APO_CONNECTION_DESCRIPTOR;
using apolite::apo::APO_CONNECTION_PROPERTY;
using apolite::apo::APO_FLAG;
using apolite::apo::APO_REG_PROPERTIES;
using apolite::apo::APO_BUFFER_FLAGS;
using apolite::apo::APO_BUFFER_VALID;
using apolite::apo::APO_BUFFER_SILENT;
using apolite::apo::APO_FLAG_DEFAULT;
using apolite::apo::APOInitSystemEffects;
using apolite::apo::IAudioSystemEffects;
using apolite::apo::IID_IAudioProcessingObject;
using apolite::apo::IID_IAudioSystemEffects2;
using apolite::apo::APOInitBaseStruct;
using apolite::apo::APOInitSystemEffects2;
using apolite::apo::IAudioProcessingObject;
using apolite::apo::IAudioProcessingObjectConfiguration;
using apolite::apo::IAudioProcessingObjectRT;
using apolite::apo::IAudioProcessingObjectVBR;
using apolite::apo::IAudioSystemEffects2;
