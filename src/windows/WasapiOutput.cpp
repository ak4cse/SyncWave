#include "WasapiOutput.h"
#include "ComHelper.h"

#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <thread>
#include <mutex>
#include <iostream>

using Microsoft::WRL::ComPtr;

namespace syncwave {

static const GUID SUBTYPE_IEEE_FLOAT_GUID = 
    { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID SUBTYPE_PCM_GUID = 
    { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

std::string outputStateToString(OutputState state) {
    switch (state) {
        case OutputState::Uninitialized: return "Uninitialized";
        case OutputState::Opened:        return "Opened";
        case OutputState::Initialized:   return "Initialized";
        case OutputState::Running:       return "Running";
        case OutputState::Stopped:       return "Stopped";
        case OutputState::Closed:        return "Closed";
        case OutputState::Error:
        default:                         return "Error";
    }
}

struct WasapiOutput::Impl {
    ComInitializer comInit;
    OutputState state = OutputState::Uninitialized;

    std::string deviceId;
    std::string deviceName;

    ComPtr<IMMDevice> pDevice;
    ComPtr<IAudioClient> pAudioClient;
    ComPtr<IAudioRenderClient> pRenderClient;
    ComPtr<IAudioClock> pAudioClock;

    AudioFormat format;
    uint32_t bufferFrameCount = 0;

    HANDLE hAudioEvent = nullptr;
    HANDLE hStopEvent = nullptr;

    std::thread renderThread;
    std::atomic<bool> running{false};

    std::atomic<uint64_t> framesRendered{0};
    std::atomic<uint32_t> underruns{0};

    Impl() = default;

    ~Impl() {
        close();
    }

    void close() {
        stop();

        pRenderClient.Reset();
        pAudioClock.Reset();
        pAudioClient.Reset();
        pDevice.Reset();

        if (hAudioEvent) {
            CloseHandle(hAudioEvent);
            hAudioEvent = nullptr;
        }
        if (hStopEvent) {
            CloseHandle(hStopEvent);
            hStopEvent = nullptr;
        }

        state = OutputState::Closed;
    }

    void stop() {
        running.store(false, std::memory_order_release);

        if (hStopEvent) {
            SetEvent(hStopEvent);
        }

        if (renderThread.joinable()) {
            renderThread.join();
        }

        if (pAudioClient) {
            pAudioClient->Stop();
            pAudioClient->Reset();
        }

        state = OutputState::Stopped;
    }

    bool openDevice(const std::string& targetDeviceId) {
        close();
        deviceId = targetDeviceId;

        ComPtr<IMMDeviceEnumerator> pEnumerator;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(&pEnumerator)
        );

        if (FAILED(hr) || !pEnumerator) {
            std::cerr << "WASAPI: Failed to create MMDeviceEnumerator: " << formatHResult(hr) << "\n";
            state = OutputState::Error;
            return false;
        }

        if (deviceId.empty()) {
            hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
            if (FAILED(hr) || !pDevice) {
                std::cerr << "WASAPI: Failed to get default render endpoint: " << formatHResult(hr) << "\n";
                state = OutputState::Error;
                return false;
            }
            LPWSTR pstrId = nullptr;
            if (SUCCEEDED(pDevice->GetId(&pstrId)) && pstrId) {
                deviceId = wideToUtf8(pstrId);
                CoTaskMemFree(pstrId);
            }
        } else {
            std::wstring wid = utf8ToWide(deviceId);
            hr = pEnumerator->GetDevice(wid.c_str(), &pDevice);
            if (FAILED(hr) || !pDevice) {
                std::cerr << "WASAPI: Failed to get device by ID (" << deviceId << "): " << formatHResult(hr) << "\n";
                state = OutputState::Error;
                return false;
            }
        }

        // Get friendly name
        ComPtr<IPropertyStore> pProps;
        deviceName = "Unknown Audio Device";
        if (SUCCEEDED(pDevice->OpenPropertyStore(STGM_READ, &pProps)) && pProps) {
            PROPVARIANT var;
            PropVariantInit(&var);
            if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &var)) && var.vt == VT_LPWSTR && var.pwszVal) {
                deviceName = wideToUtf8(var.pwszVal);
            }
            PropVariantClear(&var);
        }

        // Activate IAudioClient
        hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &pAudioClient);
        if (FAILED(hr) || !pAudioClient) {
            std::cerr << "WASAPI: Failed to activate IAudioClient on " << deviceName << ": " << formatHResult(hr) << "\n";
            state = OutputState::Error;
            return false;
        }

        state = OutputState::Opened;
        return true;
    }

    bool initializeClient() {
        if (state != OutputState::Opened || !pAudioClient) {
            std::cerr << "WASAPI error: Device must be in Opened state before initialization.\n";
            return false;
        }

        WAVEFORMATEX* pwfx = nullptr;
        HRESULT hr = pAudioClient->GetMixFormat(&pwfx);
        if (FAILED(hr) || !pwfx) {
            std::cerr << "WASAPI: GetMixFormat failed on " << deviceName << ": " << formatHResult(hr) << "\n";
            state = OutputState::Error;
            return false;
        }

        // Map WAVEFORMATEX / WAVEFORMATEXTENSIBLE to AudioFormat
        format.sampleRate = pwfx->nSamplesPerSec;
        format.channels = pwfx->nChannels;
        format.bitsPerSample = pwfx->wBitsPerSample;
        format.blockAlign = pwfx->nBlockAlign;
        format.validBitsPerSample = pwfx->wBitsPerSample;
        format.sampleType = SampleType::Unknown;

        if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            auto* pEx = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx);
            format.validBitsPerSample = pEx->Samples.wValidBitsPerSample;
            if (InlineIsEqualGUID(pEx->SubFormat, SUBTYPE_IEEE_FLOAT_GUID)) {
                format.sampleType = SampleType::Float32;
            } else if (InlineIsEqualGUID(pEx->SubFormat, SUBTYPE_PCM_GUID)) {
                if (format.bitsPerSample == 16) {
                    format.sampleType = SampleType::Int16;
                } else if (format.bitsPerSample == 24) {
                    format.sampleType = SampleType::Int24In32;
                } else if (format.bitsPerSample == 32) {
                    format.sampleType = SampleType::Int32;
                }
            }
        } else if (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
            format.sampleType = SampleType::Float32;
        } else if (pwfx->wFormatTag == WAVE_FORMAT_PCM) {
            if (format.bitsPerSample == 16) {
                format.sampleType = SampleType::Int16;
            } else if (format.bitsPerSample == 32) {
                format.sampleType = SampleType::Int32;
            }
        }

        // Initialize IAudioClient in shared event-driven mode
        hr = pAudioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            0,
            0,
            pwfx,
            nullptr
        );

        CoTaskMemFree(pwfx);

        if (FAILED(hr)) {
            std::cerr << "WASAPI: IAudioClient::Initialize failed on " << deviceName 
                      << ": " << formatHResult(hr) << "\n";
            state = OutputState::Error;
            return false;
        }

        // Create events
        hAudioEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        hStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

        if (!hAudioEvent || !hStopEvent) {
            std::cerr << "WASAPI: Failed to create synchronization event handles.\n";
            state = OutputState::Error;
            return false;
        }

        hr = pAudioClient->SetEventHandle(hAudioEvent);
        if (FAILED(hr)) {
            std::cerr << "WASAPI: SetEventHandle failed: " << formatHResult(hr) << "\n";
            state = OutputState::Error;
            return false;
        }

        hr = pAudioClient->GetBufferSize(&bufferFrameCount);
        if (FAILED(hr)) {
            std::cerr << "WASAPI: GetBufferSize failed: " << formatHResult(hr) << "\n";
            state = OutputState::Error;
            return false;
        }

        hr = pAudioClient->GetService(IID_PPV_ARGS(&pRenderClient));
        if (FAILED(hr) || !pRenderClient) {
            std::cerr << "WASAPI: GetService(IAudioRenderClient) failed: " << formatHResult(hr) << "\n";
            state = OutputState::Error;
            return false;
        }

        // Audio clock is optional/advisory
        pAudioClient->GetService(IID_PPV_ARGS(&pAudioClock));

        state = OutputState::Initialized;
        return true;
    }

    void renderThreadLoop(RenderCallback callback) {
        ComInitializer threadComInit(COINIT_MULTITHREADED);

        DWORD taskIndex = 0;
        HANDLE hMmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
        if (!hMmcss) {
            hMmcss = AvSetMmThreadCharacteristicsW(L"Playback", &taskIndex);
        }

        HANDLE waitHandles[2] = { hStopEvent, hAudioEvent };

        // Pre-roll buffer with initial callback audio
        if (pRenderClient && bufferFrameCount > 0) {
            BYTE* pData = nullptr;
            HRESULT hr = pRenderClient->GetBuffer(bufferFrameCount, &pData);
            if (SUCCEEDED(hr) && pData) {
                callback(pData, bufferFrameCount, format);
                pRenderClient->ReleaseBuffer(bufferFrameCount, 0);
                framesRendered.fetch_add(bufferFrameCount, std::memory_order_relaxed);
            }
        }

        HRESULT hrStart = pAudioClient->Start();
        if (FAILED(hrStart)) {
            std::cerr << "WASAPI: IAudioClient::Start failed: " << formatHResult(hrStart) << "\n";
            if (hMmcss) AvRevertMmThreadCharacteristics(hMmcss);
            return;
        }

        while (running.load(std::memory_order_acquire)) {
            DWORD waitRes = WaitForMultipleObjects(2, waitHandles, FALSE, 2000);
            if (waitRes == WAIT_OBJECT_0) {
                // Stop event signaled
                break;
            } else if (waitRes == WAIT_OBJECT_0 + 1) {
                // Audio buffer ready
                UINT32 padding = 0;
                HRESULT hr = pAudioClient->GetCurrentPadding(&padding);
                if (FAILED(hr)) {
                    if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                        std::cerr << "WASAPI: Audio device invalidated during playback (" << deviceName << ")\n";
                    }
                    break;
                }

                UINT32 available = (bufferFrameCount > padding) ? (bufferFrameCount - padding) : 0;
                if (available > 0) {
                    BYTE* pData = nullptr;
                    hr = pRenderClient->GetBuffer(available, &pData);
                    if (SUCCEEDED(hr) && pData) {
                        callback(pData, available, format);
                        pRenderClient->ReleaseBuffer(available, 0);
                        framesRendered.fetch_add(available, std::memory_order_relaxed);
                    } else if (FAILED(hr)) {
                        underruns.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            } else if (waitRes == WAIT_TIMEOUT) {
                underruns.fetch_add(1, std::memory_order_relaxed);
            } else {
                break;
            }
        }

        if (hMmcss) {
            AvRevertMmThreadCharacteristics(hMmcss);
        }
    }

    bool startPlayback(RenderCallback callback) {
        if (state != OutputState::Initialized && state != OutputState::Stopped) {
            std::cerr << "WASAPI error: Output must be Initialized or Stopped to start.\n";
            return false;
        }

        if (!pAudioClient || !pRenderClient) {
            return false;
        }

        framesRendered.store(0);
        underruns.store(0);
        ResetEvent(hStopEvent);

        if (pAudioClient) {
            pAudioClient->Reset();
        }

        running.store(true, std::memory_order_release);
        renderThread = std::thread(&Impl::renderThreadLoop, this, std::move(callback));

        state = OutputState::Running;
        return true;
    }
};

WasapiOutput::WasapiOutput()
    : impl_(std::make_unique<Impl>()) {}

WasapiOutput::~WasapiOutput() = default;
WasapiOutput::WasapiOutput(WasapiOutput&&) noexcept = default;
WasapiOutput& WasapiOutput::operator=(WasapiOutput&&) noexcept = default;

bool WasapiOutput::open(const std::string& deviceId) {
    if (!impl_) return false;
    return impl_->openDevice(deviceId);
}

bool WasapiOutput::initialize() {
    if (!impl_) return false;
    return impl_->initializeClient();
}

bool WasapiOutput::start(RenderCallback callback) {
    if (!impl_) return false;
    return impl_->startPlayback(std::move(callback));
}

void WasapiOutput::stop() {
    if (impl_) {
        impl_->stop();
    }
}

void WasapiOutput::close() {
    if (impl_) {
        impl_->close();
    }
}

OutputState WasapiOutput::state() const {
    return impl_ ? impl_->state : OutputState::Uninitialized;
}

AudioFormat WasapiOutput::format() const {
    return impl_ ? impl_->format : AudioFormat{};
}

uint32_t WasapiOutput::bufferFrameCount() const {
    return impl_ ? impl_->bufferFrameCount : 0;
}

uint64_t WasapiOutput::framesRendered() const {
    return impl_ ? impl_->framesRendered.load(std::memory_order_relaxed) : 0;
}

uint32_t WasapiOutput::underruns() const {
    return impl_ ? impl_->underruns.load(std::memory_order_relaxed) : 0;
}

std::string WasapiOutput::deviceName() const {
    return impl_ ? impl_->deviceName : "";
}

std::string WasapiOutput::deviceId() const {
    return impl_ ? impl_->deviceId : "";
}

std::pair<uint64_t, uint64_t> WasapiOutput::getClockPosition() const {
    if (!impl_ || !impl_->pAudioClock) {
        uint64_t rendered = framesRendered();
        uint64_t rate = (impl_ && impl_->format.sampleRate > 0) ? impl_->format.sampleRate : 48000;
        return { rendered, rate };
    }

    UINT64 pos = 0, qpc = 0, freq = 0;
    if (SUCCEEDED(impl_->pAudioClock->GetPosition(&pos, &qpc)) &&
        SUCCEEDED(impl_->pAudioClock->GetFrequency(&freq)) && freq > 0) {
        return { pos, freq };
    }

    return { framesRendered(), impl_->format.sampleRate };
}

WasapiClockSnapshot WasapiOutput::getClockSnapshot() const {
    WasapiClockSnapshot snap;
    if (!impl_) {
        return snap;
    }

    snap.framesRendered = framesRendered();
    snap.underruns = underruns();
    snap.sampleRate = impl_->format.sampleRate;
    snap.bufferFrameCount = impl_->bufferFrameCount;

    if (impl_->pAudioClient) {
        UINT32 padding = 0;
        if (SUCCEEDED(impl_->pAudioClient->GetCurrentPadding(&padding))) {
            snap.currentPadding = padding;
        }

        REFERENCE_TIME latencyRef = 0;
        if (SUCCEEDED(impl_->pAudioClient->GetStreamLatency(&latencyRef))) {
            snap.streamLatencyHns = latencyRef;
        }
    }

    if (impl_->pAudioClock) {
        UINT64 pos = 0, qpc = 0, freq = 0;
        if (SUCCEEDED(impl_->pAudioClock->GetPosition(&pos, &qpc)) &&
            SUCCEEDED(impl_->pAudioClock->GetFrequency(&freq)) && freq > 0) {
            snap.position = pos;
            snap.qpcPosition = qpc;
            snap.frequency = freq;
            snap.isValid = true;
            return snap;
        }
    }

    // Fallback if IAudioClock query failed or is uninitialized
    snap.position = snap.framesRendered;
    snap.frequency = snap.sampleRate > 0 ? snap.sampleRate : 48000;
    snap.isValid = (snap.framesRendered > 0 || impl_->state == OutputState::Running);
    return snap;
}

} // namespace syncwave
