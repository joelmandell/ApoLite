// ApoLite - Windows 11 system effect (APO) implementation.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "apolite/engine.h"
#include "apo_abi.h"

namespace apolite {

// Number of live APO instances; used by DllCanUnloadNow.
size_t apoInstanceCount();

// A Windows audio processing object that runs the ApoLite engine. The same
// class serves the pre-mix and post-mix registration slots; the stage is
// reported to the engine so that Stage filters can be evaluated.
class ApoLiteApo final : public apo::IAudioProcessingObject,
                         public apo::IAudioProcessingObjectConfiguration,
                         public apo::IAudioProcessingObjectRT,
                         public apo::IAudioProcessingObjectVBR,
                         public apo::IAudioSystemEffects2 {
public:
    ApoLiteApo();

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    // IAudioProcessingObject
    STDMETHOD(Initialize)(UINT32 cbDataSize, BYTE* pbyData) override;
    STDMETHOD(GetLatency)(HNSTIME* pTime) override;
    STDMETHOD(GetRegistrationProperties)(APO_REG_PROPERTIES** ppRegProps) override;
    STDMETHOD(IsInputFormatSupported)(struct IAudioMediaType* pOppositeFormat,
                                      struct IAudioMediaType* pRequestedInputFormat,
                                      struct IAudioMediaType** ppSupportedInputFormat) override;
    STDMETHOD(IsOutputFormatSupported)(struct IAudioMediaType* pOppositeFormat,
                                       struct IAudioMediaType* pRequestedOutputFormat,
                                       struct IAudioMediaType** ppSupportedOutputFormat) override;
    STDMETHOD(GetInputChannelCount)(UINT32* pu32ChannelCount) override;

    // IAudioProcessingObjectConfiguration
    STDMETHOD(LockForProcess)(UINT32 u32NumInputConnections,
                              APO_CONNECTION_DESCRIPTOR** ppInputConnections,
                              UINT32 u32NumOutputConnections,
                              APO_CONNECTION_DESCRIPTOR** ppOutputConnections) override;
    STDMETHOD(UnlockForProcess)(void) override;

    // IAudioProcessingObjectRT
    STDMETHOD_(void, APOProcess)
    (UINT32 u32NumInputConnections, APO_CONNECTION_PROPERTY** ppInputConnections,
     UINT32 u32NumOutputConnections, APO_CONNECTION_PROPERTY** ppOutputConnections) override;
    STDMETHOD_(UINT32, CalcInputFrames)(UINT32 u32OutputFrameCount) override;
    STDMETHOD_(UINT32, CalcOutputFrames)(UINT32 u32InputFrameCount) override;

    // IAudioProcessingObjectVBR
    STDMETHOD(CalcMaxInputFrames)(UINT32 u32MaxOutputFrameCount,
                                  UINT32* pu32InputFrameCount) override;
    STDMETHOD(CalcMaxOutputFrames)(UINT32 u32MaxInputFrameCount,
                                   UINT32* pu32OutputFrameCount) override;

    // IAudioSystemEffects / IAudioSystemEffects2
    STDMETHOD(GetEffectsList)(LPGUID* ppEffectsIds, UINT* pcEffects, HANDLE Event) override;

private:
    ~ApoLiteApo();

    void loadConfiguration();
    void startWatcher();
    void stopWatcher();
    std::wstring configPath() const;
    std::wstring logPath() const;
    // Write one line to the log file, so a dropped filter is visible without a
    // debugger attached. Also sent to the debugger when one is present.
    void logMessage(const std::string& message);

    std::atomic<ULONG> refCount_{1};

    std::mutex stateMutex_;
    Engine engine_;

    bool initialized_ = false;
    bool locked_ = false;
    bool isPreMix_ = false;
    bool isCapture_ = false;

    // Read on the audio thread in APOProcess and written by LockForProcess, so
    // these must be atomic: the audio thread cannot take stateMutex_.
    std::atomic<bool> isFloatFormat_{true};
    std::atomic<UINT32> inputChannels_{0};
    std::atomic<UINT32> outputChannels_{0};
    std::atomic<UINT32> maxFrames_{0};

    UINT32 channelMask_ = 0;
    double sampleRate_ = 48000.0;

    // Conversion staging for the non-in-place path. Sized in LockForProcess so
    // the audio thread never allocates.
    std::vector<float> scratch_;

    std::wstring deviceName_;
    std::wstring connectionName_;
    std::wstring deviceGuid_;

    std::thread watcher_;
    std::atomic<bool> watcherRunning_{false};
    std::atomic<bool> watcherStop_{false};
};

}  // namespace apolite
