// ApoLite - Windows 11 system effect (APO) implementation.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include "apo.h"

#include <audioclient.h>
#include <ksmedia.h>
#include <mmreg.h>

#include <algorithm>
#include <cstring>

#include "apolite/apolite.h"

namespace apolite {

namespace {

// Define the interface IDs declared in apo_abi.h.
const GUID kIID_IAudioProcessingObjectRT = {
    0x9E1D6A6D, 0xDDBC, 0x4E95, {0xA4, 0xC7, 0xAD, 0x64, 0xBA, 0x37, 0x84, 0x6C}};
const GUID kIID_IAudioProcessingObjectVBR = {
    0x7BA1DB8F, 0x78AD, 0x49CD, {0x95, 0x91, 0xF7, 0x9D, 0x80, 0xA1, 0x7C, 0x81}};
const GUID kIID_IAudioProcessingObjectConfiguration = {
    0x0E5ED805, 0xABA6, 0x49C3, {0x8F, 0x9A, 0x2B, 0x8C, 0x88, 0x9C, 0x4F, 0xA8}};
const GUID kIID_IAudioProcessingObject = {
    0xFD7F2B29, 0x24D0, 0x4B5C, {0xB1, 0x77, 0x59, 0x2C, 0x39, 0xF9, 0xCA, 0x10}};
const GUID kIID_IAudioSystemEffects = {
    0x5FA00F27, 0xADD6, 0x499A, {0x8A, 0x9D, 0x6B, 0x98, 0x52, 0x1F, 0xA7, 0x5B}};
const GUID kIID_IAudioSystemEffects2 = {
    0xBAFE99D2, 0x7436, 0x44CE, {0x9E, 0x0E, 0x4D, 0x89, 0xAF, 0xBF, 0xFF, 0x56}};

// KSDATAFORMAT_SUBTYPE_PCM / _IEEE_FLOAT, normally from ksmedia.h.
const GUID kSubtypePcm = {
    0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
const GUID kSubtypeIeeeFloat = {
    0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

bool guidEquals(REFGUID a, REFGUID b) {
    return std::memcmp(&a, &b, sizeof(GUID)) == 0;
}

std::string toNarrow(const wchar_t* s) {
    if (s == nullptr || *s == L'\0') return std::string();
    const int size = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size > 0 ? size - 1 : 0), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), size, nullptr, nullptr);
    return out;
}

// The audio engine hands the endpoint GUID to every APO. An endpoint is a
// capture device when Windows lists it under the MMDevices Capture tree.
bool endpointIsCapture(const std::wstring& endpointGuid) {
    if (endpointGuid.empty()) return false;
    const std::wstring base =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Capture\\";
    const std::wstring key = base + endpointGuid;
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        RegCloseKey(hkey);
        return true;
    }
    return false;
}

float int32ToFloat(int32_t v) {
    return static_cast<float>(static_cast<double>(v) / 2147483648.0);
}

int32_t floatToInt32(float v) {
    const double scaled = static_cast<double>(v) * 2147483648.0;
    if (scaled >= 2147483647.0) return 2147483647;
    if (scaled <= -2147483648.0) return -2147483647 - 1;
    return static_cast<int32_t>(scaled);
}

// Channel mask to Equalizer APO channel order (L, R, C, LFE, SL, SR, ...).
const UINT32 kChannelMaskOrder[] = {
    SPEAKER_FRONT_LEFT,      SPEAKER_FRONT_RIGHT,     SPEAKER_FRONT_CENTER,
    SPEAKER_LOW_FREQUENCY,   SPEAKER_BACK_LEFT,       SPEAKER_BACK_RIGHT,
    SPEAKER_FRONT_LEFT_OF_CENTER, SPEAKER_FRONT_RIGHT_OF_CENTER,
    SPEAKER_BACK_CENTER,     SPEAKER_SIDE_LEFT,       SPEAKER_SIDE_RIGHT,
    SPEAKER_TOP_CENTER,      SPEAKER_TOP_FRONT_LEFT,  SPEAKER_TOP_FRONT_CENTER,
    SPEAKER_TOP_FRONT_RIGHT, SPEAKER_TOP_BACK_LEFT,   SPEAKER_TOP_BACK_CENTER,
    SPEAKER_TOP_BACK_RIGHT,
};

}  // namespace

std::atomic<size_t> g_apoInstances{0};

size_t apoInstanceCount() { return g_apoInstances.load(); }

ApoLiteApo::ApoLiteApo() {
    g_apoInstances.fetch_add(1);
    engine_.setLogSink([](const std::string& message) {
        OutputDebugStringA(("ApoLite: " + message + "\n").c_str());
    });
}

ApoLiteApo::~ApoLiteApo() {
    stopWatcher();
    g_apoInstances.fetch_sub(1);
}

std::wstring ApoLiteApo::configPath() const {
    wchar_t programData[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableW(L"ProgramData", programData, MAX_PATH);
    std::wstring base = (len > 0 && len < MAX_PATH) ? std::wstring(programData, len)
                                                    : std::wstring(L"C:\\ProgramData");
    return base + L"\\ApoLite\\config\\config.txt";
}

// ------------------------------------------------------------------ IUnknown

STDMETHODIMP ApoLiteApo::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    if (riid == __uuidof(IUnknown) || riid == kIID_IAudioProcessingObject) {
        *ppv = static_cast<apo::IAudioProcessingObject*>(this);
    } else if (riid == kIID_IAudioProcessingObjectConfiguration) {
        *ppv = static_cast<apo::IAudioProcessingObjectConfiguration*>(this);
    } else if (riid == kIID_IAudioProcessingObjectRT) {
        *ppv = static_cast<apo::IAudioProcessingObjectRT*>(this);
    } else if (riid == kIID_IAudioProcessingObjectVBR) {
        *ppv = static_cast<apo::IAudioProcessingObjectVBR*>(this);
    } else if (riid == kIID_IAudioSystemEffects2) {
        *ppv = static_cast<apo::IAudioSystemEffects2*>(this);
    } else if (riid == kIID_IAudioSystemEffects) {
        *ppv = static_cast<apo::IAudioSystemEffects*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) ApoLiteApo::AddRef() {
    return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
}

STDMETHODIMP_(ULONG) ApoLiteApo::Release() {
    const ULONG remaining = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0) delete this;
    return remaining;
}

// ------------------------------------------------- IAudioProcessingObject

STDMETHODIMP ApoLiteApo::Initialize(UINT32 cbDataSize, BYTE* pbyData) {
    if (pbyData == nullptr || cbDataSize < sizeof(APOInitBaseStruct)) return E_POINTER;

    std::lock_guard<std::mutex> lock(stateMutex_);
    if (initialized_) return apo::kApoErrAlreadyInitialized;

    auto* pInit = reinterpret_cast<APOInitBaseStruct*>(pbyData);
    if (pInit->cbSize > cbDataSize) return E_INVALIDARG;

    // The audio engine instantiates a distinct CLSID per pipeline slot, so the
    // CLSID we were created with tells us whether this is a pre-mix instance.
    isPreMix_ = guidEquals(pInit->clsid, apolite::kPreMixClsid);

    if (cbDataSize >= sizeof(APOInitSystemEffects)) {
        auto* effects = reinterpret_cast<APOInitSystemEffects*>(pbyData);
        if (effects->pAPOEndpointProperties != nullptr) {
            // PKEY_Device_FriendlyName
            const PROPERTYKEY friendlyName = {
                {0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};
            // PKEY_AudioEndpoint_GUID
            const PROPERTYKEY endpointGuid = {
                {0x1DA5D803, 0xD492, 0x4EDD, {0x8C, 0x23, 0xE0, 0xC0, 0xFF, 0xEE, 0x7F, 0x0E}}, 4};

            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(effects->pAPOEndpointProperties->GetValue(friendlyName, &value)) &&
                value.vt == VT_LPWSTR) {
                deviceName_ = value.pwszVal;
            }
            PropVariantClear(&value);

            PropVariantInit(&value);
            if (SUCCEEDED(effects->pAPOEndpointProperties->GetValue(endpointGuid, &value)) &&
                value.vt == VT_LPWSTR) {
                deviceGuid_ = value.pwszVal;
            }
            PropVariantClear(&value);
        }
    }

    initialized_ = true;
    isCapture_ = endpointIsCapture(deviceGuid_);
    loadConfiguration();
    startWatcher();
    return S_OK;
}

STDMETHODIMP ApoLiteApo::GetLatency(HNSTIME* pTime) {
    if (pTime == nullptr) return E_POINTER;
    *pTime = 0;
    return S_OK;
}

STDMETHODIMP ApoLiteApo::GetRegistrationProperties(APO_REG_PROPERTIES** ppRegProps) {
    if (ppRegProps == nullptr) return E_POINTER;

    // iidAPOInterfaceList is declared as [1] but holds u32NumAPOInterfaces entries,
    // so allocate room for the extra interface ID.
    constexpr UINT32 kInterfaces = 2;
    const size_t size = sizeof(APO_REG_PROPERTIES) + (kInterfaces - 1) * sizeof(IID);
    auto* props = static_cast<APO_REG_PROPERTIES*>(CoTaskMemAlloc(size));
    if (props == nullptr) return E_OUTOFMEMORY;

    std::memset(props, 0, size);
    props->clsid = isPreMix_ ? apolite::kPreMixClsid : apolite::kPostMixClsid;
    props->Flags = apo::APO_FLAG_DEFAULT;
    wcscpy_s(props->szFriendlyName, L"ApoLite");
    wcscpy_s(props->szCopyrightInfo, L"MIT licensed. Copyright (c) 2026 Joel Mandell.");
    props->u32MajorVersion = apolite::kVersionMajor;
    props->u32MinorVersion = apolite::kVersionMinor;
    props->u32MinInputConnections = 1;
    props->u32MaxInputConnections = 1;
    props->u32MinOutputConnections = 1;
    props->u32MaxOutputConnections = 1;
    props->u32MaxInstances = 0;
    props->u32NumAPOInterfaces = kInterfaces;
    props->iidAPOInterfaceList[0] = kIID_IAudioProcessingObject;
    props->iidAPOInterfaceList[1] = kIID_IAudioSystemEffects2;

    *ppRegProps = props;
    return S_OK;
}

STDMETHODIMP ApoLiteApo::IsInputFormatSupported(struct IAudioMediaType* pOppositeFormat,
                                                struct IAudioMediaType* pRequestedInputFormat,
                                                struct IAudioMediaType** ppSupportedInputFormat) {
    if (ppSupportedInputFormat != nullptr) *ppSupportedInputFormat = nullptr;
    if (pRequestedInputFormat == nullptr) return E_POINTER;
    // ApoLite accepts whatever the engine offers; LockForProcess validates.
    (void)pOppositeFormat;
    return S_OK;
}

STDMETHODIMP ApoLiteApo::IsOutputFormatSupported(struct IAudioMediaType* pOppositeFormat,
                                                 struct IAudioMediaType* pRequestedOutputFormat,
                                                 struct IAudioMediaType** ppSupportedOutputFormat) {
    if (ppSupportedOutputFormat != nullptr) *ppSupportedOutputFormat = nullptr;
    if (pRequestedOutputFormat == nullptr) return E_POINTER;
    (void)pOppositeFormat;
    return S_OK;
}

STDMETHODIMP ApoLiteApo::GetInputChannelCount(UINT32* pu32ChannelCount) {
    if (pu32ChannelCount == nullptr) return E_POINTER;
    std::lock_guard<std::mutex> lock(stateMutex_);
    *pu32ChannelCount = inputChannels_ != 0 ? inputChannels_ : 2;
    return S_OK;
}

// ------------------------------------ IAudioProcessingObjectConfiguration

STDMETHODIMP ApoLiteApo::LockForProcess(UINT32 u32NumInputConnections,
                                        APO_CONNECTION_DESCRIPTOR** ppInputConnections,
                                        UINT32 u32NumOutputConnections,
                                        APO_CONNECTION_DESCRIPTOR** ppOutputConnections) {
    if (u32NumInputConnections != 1 || u32NumOutputConnections != 1) {
        return apo::kApoErrNumConnectionsInvalid;
    }
    if (ppInputConnections == nullptr || ppOutputConnections == nullptr) return E_POINTER;

    APO_CONNECTION_DESCRIPTOR* in = ppInputConnections[0];
    APO_CONNECTION_DESCRIPTOR* out = ppOutputConnections[0];
    if (in == nullptr || out == nullptr) return E_POINTER;

    auto* inFormat = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(in->pFormat);
    auto* outFormat = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(out->pFormat);
    if (inFormat == nullptr || outFormat == nullptr) return E_POINTER;

    std::lock_guard<std::mutex> lock(stateMutex_);
    if (locked_) return apo::kApoErrAlreadyInitialized;

    if (inFormat->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        inputChannels_ = inFormat->Format.nChannels;
        isFloatFormat_ = guidEquals(inFormat->SubFormat, kSubtypeIeeeFloat);
        channelMask_ = inFormat->dwChannelMask;
    } else {
        inputChannels_ = inFormat->Format.nChannels;
        isFloatFormat_ = inFormat->Format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        channelMask_ = 0;
    }

    outputChannels_ = outFormat->Format.nChannels;
    sampleRate_ = static_cast<double>(inFormat->Format.nSamplesPerSec);
    maxFrames_ = std::max(in->u32MaxFrameCount, out->u32MaxFrameCount);
    if (maxFrames_ == 0) maxFrames_ = 1024;

    engine_.configure(sampleRate_, inputChannels_, outputChannels_, channelMask_, isCapture_,
                      isPreMix_, /*postMixInstalled=*/true, toNarrow(deviceName_.c_str()),
                      toNarrow(connectionName_.c_str()), toNarrow(deviceGuid_.c_str()));
    engine_.setMaxFrames(maxFrames_);
    loadConfiguration();

    locked_ = true;
    return S_OK;
}

STDMETHODIMP ApoLiteApo::UnlockForProcess() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!locked_) return apo::kApoErrAlreadyUnlocked;
    locked_ = false;
    return S_OK;
}

// --------------------------------------------- IAudioProcessingObjectRT

STDMETHODIMP_(void)
ApoLiteApo::APOProcess(UINT32 u32NumInputConnections, APO_CONNECTION_PROPERTY** ppInputConnections,
                       UINT32 u32NumOutputConnections,
                       APO_CONNECTION_PROPERTY** ppOutputConnections) {
    if (u32NumInputConnections < 1 || u32NumOutputConnections < 1) return;
    if (ppInputConnections == nullptr || ppOutputConnections == nullptr) return;

    APO_CONNECTION_PROPERTY* in = ppInputConnections[0];
    APO_CONNECTION_PROPERTY* out = ppOutputConnections[0];
    if (in == nullptr || out == nullptr) return;

    const UINT32 frames = in->u32ValidFrameCount;
    if (frames == 0 || out->u32ValidFrameCount < frames) return;

    auto* src = reinterpret_cast<const float*>(in->pBuffer);
    auto* dst = reinterpret_cast<float*>(out->pBuffer);
    if (src == nullptr || dst == nullptr) return;

    const size_t channels = outputChannels_ != 0 ? outputChannels_ : inputChannels_;

    // Fast path: float, in place, no channel conversion.
    if (isFloatFormat_ && src == dst && channels != 0) {
        engine_.processInterleaved(dst, frames);
        out->u32BufferFlags = apo::APO_BUFFER_VALID;
        return;
    }

    // Integer PCM or separate buffers: convert into a scratch float buffer.
    if (scratch_.size() < static_cast<size_t>(frames) * channels)
        scratch_.resize(static_cast<size_t>(frames) * channels);

    if (isFloatFormat_) {
        std::memcpy(scratch_.data(), src, static_cast<size_t>(frames) * channels * sizeof(float));
    } else {
        const auto* intSrc = reinterpret_cast<const int32_t*>(src);
        for (size_t i = 0; i < static_cast<size_t>(frames) * channels; ++i)
            scratch_[i] = int32ToFloat(intSrc[i]);
    }

    engine_.processInterleaved(scratch_.data(), frames);

    if (isFloatFormat_) {
        std::memcpy(dst, scratch_.data(), static_cast<size_t>(frames) * channels * sizeof(float));
    } else {
        auto* intDst = reinterpret_cast<int32_t*>(dst);
        for (size_t i = 0; i < static_cast<size_t>(frames) * channels; ++i)
            intDst[i] = floatToInt32(scratch_[i]);
    }
    out->u32BufferFlags = apo::APO_BUFFER_VALID;
}

STDMETHODIMP_(UINT32) ApoLiteApo::CalcInputFrames(UINT32 u32OutputFrameCount) {
    return u32OutputFrameCount;
}

STDMETHODIMP_(UINT32) ApoLiteApo::CalcOutputFrames(UINT32 u32InputFrameCount) {
    return u32InputFrameCount;
}

// -------------------------------------------- IAudioProcessingObjectVBR

STDMETHODIMP ApoLiteApo::CalcMaxInputFrames(UINT32 u32MaxOutputFrameCount,
                                            UINT32* pu32InputFrameCount) {
    if (pu32InputFrameCount == nullptr) return E_POINTER;
    *pu32InputFrameCount = u32MaxOutputFrameCount;
    return S_OK;
}

STDMETHODIMP ApoLiteApo::CalcMaxOutputFrames(UINT32 u32MaxInputFrameCount,
                                             UINT32* pu32OutputFrameCount) {
    if (pu32OutputFrameCount == nullptr) return E_POINTER;
    *pu32OutputFrameCount = u32MaxInputFrameCount;
    return S_OK;
}

// ------------------------------------ IAudioSystemEffects / IAudioSystemEffects2

STDMETHODIMP ApoLiteApo::GetEffectsList(LPGUID* ppEffectsIds, UINT* pcEffects, HANDLE Event) {
    if (ppEffectsIds == nullptr || pcEffects == nullptr) return E_POINTER;
    // No sub-effect UI is exposed; ApoLite is configured through config.txt.
    *ppEffectsIds = nullptr;
    *pcEffects = 0;
    (void)Event;
    return S_OK;
}

// ------------------------------------------------------- configuration

void ApoLiteApo::loadConfiguration() {
    const std::wstring path = configPath();
    const std::string narrow = toNarrow(path.c_str());
    engine_.setLogSink([](const std::string& message) {
        OutputDebugStringA(("ApoLite: " + message + "\n").c_str());
    });
    engine_.loadConfiguration(narrow);
}

void ApoLiteApo::startWatcher() {
    if (watcherRunning_.exchange(true)) return;
    watcherStop_ = false;
    watcher_ = std::thread([this]() {
        const std::wstring path = configPath();
        HANDLE dir = CreateFileW(path.substr(0, path.find_last_of(L'\\')).c_str(),
                                 FILE_LIST_DIRECTORY,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (dir == INVALID_HANDLE_VALUE) {
            watcherRunning_ = false;
            return;
        }

        while (!watcherStop_.load()) {
            DWORD bytes = 0;
            alignas(DWORD) char buffer[4096];
            const BOOL ok = ReadDirectoryChangesW(
                dir, buffer, sizeof(buffer), FALSE,
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE |
                    FILE_NOTIFY_CHANGE_FILE_NAME,
                &bytes, nullptr, nullptr);
            if (!ok) break;
            if (bytes == 0) continue;
            // Debounce: let the editor finish writing.
            Sleep(150);
            loadConfiguration();
        }
        CloseHandle(dir);
        watcherRunning_ = false;
    });
}

void ApoLiteApo::stopWatcher() {
    if (!watcherRunning_.load()) return;
    watcherStop_ = true;
    // Wake ReadDirectoryChangesW by touching the directory.
    const std::wstring path = configPath();
    HANDLE dir = CreateFileW(path.substr(0, path.find_last_of(L'\\')).c_str(),
                             FILE_LIST_DIRECTORY,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                             OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (dir != INVALID_HANDLE_VALUE) {
        CancelSynchronousIo(watcher_.native_handle());
        CloseHandle(dir);
    }
    if (watcher_.joinable()) watcher_.join();
    watcherRunning_ = false;
}

}  // namespace apolite
