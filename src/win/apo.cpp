// ApoLite - Windows 11 system effect (APO) implementation.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#include "apo.h"

#include <audioclient.h>
#include <ksmedia.h>
#include <mmreg.h>

#include <algorithm>
#include <cstdio>
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
    engine_.setLogSink([this](const std::string& message) { logMessage(message); });
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

std::wstring ApoLiteApo::logPath() const {
    const std::wstring path = configPath();
    const size_t slash = path.find_last_of(L'\\');
    const std::wstring dir = slash == std::wstring::npos ? path : path.substr(0, slash);
    return dir + L"\\apolite.log";
}

// Write one line to `path`, rotating first if it has grown large. Returns false
// when the file cannot be opened, which the caller may answer with another path.
static bool writeLogLine(const std::wstring& path, const std::string& message) {
    const bool fresh = GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES;

    // Start over once the file is large, so a reload loop cannot grow it without
    // bound. A few hundred kilobytes is far more history than a user needs.
    constexpr ULONGLONG kMaxLogBytes = 256 * 1024;
    if (!fresh) {
        WIN32_FILE_ATTRIBUTE_DATA attr = {};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr)) {
            const ULONGLONG size =
                (static_cast<ULONGLONG>(attr.nFileSizeHigh) << 32) | attr.nFileSizeLow;
            if (size > kMaxLogBytes) DeleteFileW(path.c_str());
        }
    }

    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    if (fresh) {
        const char* header = "ApoLite log. A filter that is dropped is reported here.\n";
        DWORD wrote = 0;
        WriteFile(file, header, static_cast<DWORD>(std::strlen(header)), &wrote, nullptr);
    }

    SYSTEMTIME now;
    GetLocalTime(&now);
    char stamp[32] = {};
    std::snprintf(stamp, sizeof(stamp), "%04u-%02u-%02u %02u:%02u:%02u ", now.wYear, now.wMonth,
                  now.wDay, now.wHour, now.wMinute, now.wSecond);
    const std::string line = std::string(stamp) + message + "\n";
    DWORD wrote = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &wrote, nullptr);
    CloseHandle(file);
    return true;
}

void ApoLiteApo::logMessage(const std::string& message) {
    OutputDebugStringA(("ApoLite: " + message + "\n").c_str());

    // A user without a debugger still needs to see why a filter was dropped, so
    // the same line goes to a small file beside the configuration. The audio
    // process runs as a service account and may not be allowed to write there,
    // so fall back to the temp directory rather than dropping the line.
    if (writeLogLine(logPath(), message)) return;

    wchar_t temp[MAX_PATH] = {};
    const DWORD n = GetTempPathW(MAX_PATH, temp);
    if (n == 0 || n >= MAX_PATH) return;
    writeLogLine(std::wstring(temp) + L"apolite.log", message);
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
    const UINT32 inCh = inputChannels_.load(std::memory_order_relaxed);
    *pu32ChannelCount = inCh != 0 ? inCh : 2;
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
        inputChannels_.store(inFormat->Format.nChannels, std::memory_order_relaxed);
        isFloatFormat_.store(guidEquals(inFormat->SubFormat, kSubtypeIeeeFloat),
                             std::memory_order_relaxed);
        channelMask_ = inFormat->dwChannelMask;
    } else {
        inputChannels_.store(inFormat->Format.nChannels, std::memory_order_relaxed);
        isFloatFormat_.store(inFormat->Format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT,
                             std::memory_order_relaxed);
        channelMask_ = 0;
    }

    outputChannels_.store(outFormat->Format.nChannels, std::memory_order_relaxed);
    sampleRate_ = static_cast<double>(inFormat->Format.nSamplesPerSec);
    maxFrames_.store(std::max(in->u32MaxFrameCount, out->u32MaxFrameCount),
                     std::memory_order_relaxed);
    if (maxFrames_.load() == 0) maxFrames_.store(1024);

    // The engine clamps a host block to its prepared size, so this only has to
    // cover the frame counts the host promised. Sizing it here keeps the audio
    // thread allocation-free.
    const size_t channels =
        std::max<UINT32>(inputChannels_.load(), outputChannels_.load());
    scratch_.assign(static_cast<size_t>(maxFrames_.load()) * std::max<size_t>(channels, 1), 0.0f);

    engine_.configure(sampleRate_, inputChannels_.load(), outputChannels_.load(), channelMask_,
                      isCapture_, isPreMix_, /*postMixInstalled=*/true, toNarrow(deviceName_.c_str()),
                      toNarrow(connectionName_.c_str()), toNarrow(deviceGuid_.c_str()));
    engine_.setMaxFrames(maxFrames_.load());
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
    if (frames == 0) {
        out->u32BufferFlags = apo::APO_BUFFER_SILENT;
        return;
    }
    // A host that asks for more frames than it promised is a host error; leaving
    // the output untouched is the only safe response, but say so in the flags.
    if (out->u32ValidFrameCount < frames) {
        out->u32BufferFlags = apo::APO_BUFFER_SILENT;
        return;
    }

    auto* src = reinterpret_cast<const float*>(in->pBuffer);
    auto* dst = reinterpret_cast<float*>(out->pBuffer);
    if (src == nullptr || dst == nullptr) return;

    const bool isFloat = isFloatFormat_.load(std::memory_order_relaxed);
    const UINT32 inCh = inputChannels_.load(std::memory_order_relaxed);
    const UINT32 outCh = outputChannels_.load(std::memory_order_relaxed);
    const size_t channels = outCh != 0 ? outCh : inCh;
    if (channels == 0) return;

    // Fast path: float, in place, no channel conversion.
    if (isFloat && src == dst) {
        engine_.processInterleaved(dst, frames);
        out->u32BufferFlags = apo::APO_BUFFER_VALID;
        return;
    }

    // Integer PCM or separate buffers. scratch_ was sized in LockForProcess, so
    // this must not allocate: a block larger than that is handled in chunks.
    const size_t capacityFrames = scratch_.size() / channels;
    if (capacityFrames == 0) return;

    size_t done = 0;
    while (done < frames) {
        const size_t n = std::min(capacityFrames, static_cast<size_t>(frames) - done);
        const size_t samples = n * channels;
        const float* srcChunk = src + done * channels;
        float* dstChunk = dst + done * channels;

        if (isFloat) {
            std::memcpy(scratch_.data(), srcChunk, samples * sizeof(float));
        } else {
            const auto* intSrc = reinterpret_cast<const int32_t*>(srcChunk);
            for (size_t i = 0; i < samples; ++i) scratch_[i] = int32ToFloat(intSrc[i]);
        }

        engine_.processInterleaved(scratch_.data(), n);

        if (isFloat) {
            std::memcpy(dstChunk, scratch_.data(), samples * sizeof(float));
        } else {
            auto* intDst = reinterpret_cast<int32_t*>(dstChunk);
            for (size_t i = 0; i < samples; ++i) intDst[i] = floatToInt32(scratch_[i]);
        }
        done += n;
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
    engine_.setLogSink([this](const std::string& message) { logMessage(message); });
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
    // The watcher clears watcherRunning_ itself when it exits, so it must not
    // gate the join: a self-exited thread is still joinable, and letting
    // std::thread's destructor run on a joinable thread calls std::terminate,
    // which takes the host audio process down.
    if (!watcher_.joinable()) return;

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
    watcher_.join();
    watcherRunning_ = false;
}

}  // namespace apolite
