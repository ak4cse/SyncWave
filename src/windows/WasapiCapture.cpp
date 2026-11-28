#include "WasapiCapture.h"
#include "ComHelper.h"

#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <thread>
#include <atomic>
#include <vector>
#include <iostream>

using Microsoft::WRL::ComPtr;

namespace syncwave {

static const GUID CAP_SUBTYPE_IEEE_FLOAT_GUID = 
    { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID CAP_SUBTYPE_PCM_GUID = 
    { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

#ifndef VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK_DEVICE_ID
#define VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK_DEVICE_ID L"VAD\\Process_Loopback"
#endif

namespace {

enum SW_AUDIOCLIENT_ACTIVATION_TYPE {
    SW_ACTIVATE_DEFAULT = 0,
    SW_ACTIVATE_PROCESS_LOOPBACK = 1
};

enum SW_PROCESS_LOOPBACK_MODE {
    SW_PROCESS_LOOPBACK_INCLUDE = 0,
    SW_PROCESS_LOOPBACK_EXCLUDE = 1
};

struct SW_AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS {
    DWORD TargetProcessId;
    SW_PROCESS_LOOPBACK_MODE ProcessLoopbackMode;
};

struct SW_AUDIOCLIENT_ACTIVATION_PARAMS {
    SW_AUDIOCLIENT_ACTIVATION_TYPE ActivationType;
    union {
        SW_AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams;
    };
};

class ProcessLoopbackCompletionHandler : public IActivateAudioInterfaceCompletionHandler {
public:
    ProcessLoopbackCompletionHandler() { hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr); }
    ~ProcessLoopbackCompletionHandler() { if (hEvent) CloseHandle(hEvent); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler) ||
            riid == IID_IAgileObject) {
            *ppv = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG c = --ref_;
        if (c == 0) delete this;
        return c;
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        HRESULT hrActivate = E_FAIL;
        IUnknown* unk = nullptr;
        if (op && SUCCEEDED(op->GetActivateResult(&hrActivate, &unk))) {
            activateHr = hrActivate;
            if (SUCCEEDED(hrActivate) && unk) {
                unk->QueryInterface(IID_PPV_ARGS(&client));
                unk->Release();
            }
        }
        if (hEvent) SetEvent(hEvent);
        return S_OK;
    }

    HANDLE hEvent{nullptr};
    HRESULT activateHr{E_FAIL};
    ComPtr<IAudioClient> client;
private:
    std::atomic<ULONG> ref_{1};
};

} // namespace

std::string captureStateToString(CaptureState state) {
    switch (state) {
        case CaptureState::Uninitialized: return "Uninitialized";
        case CaptureState::Opened:        return "Opened";
        case CaptureState::Initialized:   return "Initialized";
        case CaptureState::Running:       return "Running";
        case CaptureState::Stopped:       return "Stopped";
        case CaptureState::Closed:        return "Closed";
        case CaptureState::Error:
        default:                          return "Error";
    }
}

struct WasapiCapture::Impl {
    ComInitializer comInit;
    CaptureState state = CaptureState::Uninitialized;

    std::string deviceId;
    std::string deviceName;

    ComPtr<IMMDevice> pDevice;
    ComPtr<IAudioClient> pAudioClient;
    ComPtr<IAudioCaptureClient> pCaptureClient;
    ComPtr<IAudioClock> pAudioClock;

    bool isProcessLoopback = false;
    AudioFormat format;
    uint32_t bufferFrameCount = 0;

    HANDLE hAudioEvent = nullptr;
    HANDLE hStopEvent = nullptr;

    std::thread captureThread;
    std::atomic<bool> running{false};

    std::atomic<uint64_t> framesCaptured{0};
    std::atomic<uint64_t> packetsCaptured{0};
    std::atomic<uint32_t> silencePackets{0};
    std::atomic<uint32_t> discontinuities{0};
    std::atomic<uint32_t> captureErrors{0};

    Impl() = default;

    ~Impl() {
        close();
    }

    void close() {
        stop();

        pCaptureClient.Reset();
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

        state = CaptureState::Closed;
    }

    void stop() {
        if (!running.exchange(false, std::memory_order_acq_rel)) {
            if (state == CaptureState::Running) {
                state = CaptureState::Stopped;
            }
            return;
        }

        if (hStopEvent) {
            SetEvent(hStopEvent);
        }

        if (captureThread.joinable()) {
            captureThread.join();
        }

        if (pAudioClient) {
            pAudioClient->Stop();
        }

        state = CaptureState::Stopped;
    }

    bool openDevice(const std::string& targetDeviceId) {
        close();
        isProcessLoopback = false;
        deviceId = targetDeviceId;

        ComPtr<IMMDeviceEnumerator> pEnumerator;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(&pEnumerator)
        );

        if (FAILED(hr) || !pEnumerator) {
            std::cerr << "WASAPI Capture: Failed to create MMDeviceEnumerator: " << formatHResult(hr) << "\n";
            state = CaptureState::Error;
            return false;
        }

        if (deviceId.empty()) {
            hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
            if (FAILED(hr) || !pDevice) {
                std::cerr << "WASAPI Capture: Failed to get default render endpoint: " << formatHResult(hr) << "\n";
                state = CaptureState::Error;
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
                std::cerr << "WASAPI Capture: Failed to get device by ID (" << deviceId << "): " << formatHResult(hr) << "\n";
                state = CaptureState::Error;
                return false;
            }
        }

        // Retrieve friendly name
        ComPtr<IPropertyStore> pProps;
        deviceName = "Unknown Audio Endpoint";
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
            std::cerr << "WASAPI Capture: Failed to activate IAudioClient on " << deviceName << ": " << formatHResult(hr) << "\n";
            state = CaptureState::Error;
            return false;
        }

        state = CaptureState::Opened;
        return true;
    }

    bool openProcessLoopbackDevice(uint32_t excludePid) {
        close();
        isProcessLoopback = true;
        deviceId = "VAD\\Process_Loopback";
        deviceName = "Windows Process Loopback (All Apps)";

        HMODULE hMmdevapi = LoadLibraryW(L"mmdevapi.dll");
        if (!hMmdevapi) {
            state = CaptureState::Error;
            return false;
        }

        using ActivateAsyncFn = HRESULT(WINAPI*)(LPCWSTR, REFIID, PROPVARIANT*,
                                                 IActivateAudioInterfaceCompletionHandler*,
                                                 IActivateAudioInterfaceAsyncOperation**);
        auto pActivateAsync = reinterpret_cast<ActivateAsyncFn>(
            reinterpret_cast<void*>(GetProcAddress(hMmdevapi, "ActivateAudioInterfaceAsync")));
        if (!pActivateAsync) {
            FreeLibrary(hMmdevapi);
            state = CaptureState::Error;
            return false;
        }

        SW_AUDIOCLIENT_ACTIVATION_PARAMS params = {};
        params.ActivationType = SW_ACTIVATE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.TargetProcessId = (excludePid == 0) ? GetCurrentProcessId() : excludePid;
        params.ProcessLoopbackParams.ProcessLoopbackMode = SW_PROCESS_LOOPBACK_EXCLUDE;

        PROPVARIANT prop = {};
        prop.vt = VT_BLOB;
        prop.blob.cbSize = sizeof(params);
        prop.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

        auto* handler = new ProcessLoopbackCompletionHandler();
        ComPtr<IActivateAudioInterfaceAsyncOperation> asyncOp;
        HRESULT hr = pActivateAsync(
            VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK_DEVICE_ID,
            __uuidof(IAudioClient),
            &prop,
            handler,
            &asyncOp
        );

        bool ok = false;
        if (SUCCEEDED(hr) && handler->hEvent) {
            WaitForSingleObject(handler->hEvent, 5000);
            if (SUCCEEDED(handler->activateHr) && handler->client) {
                pAudioClient = handler->client;
                state = CaptureState::Opened;
                ok = true;
            }
        }

        handler->Release();
        FreeLibrary(hMmdevapi);
        if (!ok) {
            state = CaptureState::Error;
        }
        return ok;
    }

    bool initializeClient() {
        if (state != CaptureState::Opened || !pAudioClient) {
            std::cerr << "WASAPI Capture error: Device must be in Opened state before initialization.\n";
            return false;
        }

        HRESULT hr = S_OK;
        WAVEFORMATEX* pwfx = nullptr;
        WAVEFORMATEX processFmt = {};

        if (isProcessLoopback) {
            processFmt.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
            processFmt.nChannels = 2;
            processFmt.nSamplesPerSec = 48000;
            processFmt.wBitsPerSample = 32;
            processFmt.nBlockAlign = processFmt.nChannels * processFmt.wBitsPerSample / 8;
            processFmt.nAvgBytesPerSec = processFmt.nSamplesPerSec * processFmt.nBlockAlign;

            format.sampleRate = 48000;
            format.channels = 2;
            format.bitsPerSample = 32;
            format.blockAlign = processFmt.nBlockAlign;
            format.validBitsPerSample = 32;
            format.sampleType = SampleType::Float32;

            hr = pAudioClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                0,
                0,
                &processFmt,
                nullptr
            );
        } else {
            hr = pAudioClient->GetMixFormat(&pwfx);
            if (FAILED(hr) || !pwfx) {
                std::cerr << "WASAPI Capture: GetMixFormat failed on " << deviceName << ": " << formatHResult(hr) << "\n";
                state = CaptureState::Error;
                return false;
            }

            format.sampleRate = pwfx->nSamplesPerSec;
            format.channels = pwfx->nChannels;
            format.bitsPerSample = pwfx->wBitsPerSample;
            format.blockAlign = pwfx->nBlockAlign;
            format.validBitsPerSample = pwfx->wBitsPerSample;
            format.sampleType = SampleType::Unknown;

            if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                auto* pEx = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx);
                format.validBitsPerSample = pEx->Samples.wValidBitsPerSample;
                if (InlineIsEqualGUID(pEx->SubFormat, CAP_SUBTYPE_IEEE_FLOAT_GUID)) {
                    format.sampleType = SampleType::Float32;
                } else if (InlineIsEqualGUID(pEx->SubFormat, CAP_SUBTYPE_PCM_GUID)) {
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

            // Initialize IAudioClient in shared loopback mode with event callback
            hr = pAudioClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                0,
                0,
                pwfx,
                nullptr
            );

            CoTaskMemFree(pwfx);
        }

        if (FAILED(hr)) {
            std::cerr << "WASAPI Capture: IAudioClient::Initialize failed on " << deviceName 
                      << ": " << formatHResult(hr) << "\n";
            state = CaptureState::Error;
            return false;
        }

        hAudioEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        hStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

        if (!hAudioEvent || !hStopEvent) {
            std::cerr << "WASAPI Capture: Failed to create synchronization event handles.\n";
            state = CaptureState::Error;
            return false;
        }

        hr = pAudioClient->SetEventHandle(hAudioEvent);
        if (FAILED(hr)) {
            std::cerr << "WASAPI Capture: SetEventHandle failed: " << formatHResult(hr) << "\n";
            state = CaptureState::Error;
            return false;
        }

        hr = pAudioClient->GetBufferSize(&bufferFrameCount);
        if (FAILED(hr)) {
            std::cerr << "WASAPI Capture: GetBufferSize failed: " << formatHResult(hr) << "\n";
            state = CaptureState::Error;
            return false;
        }

        hr = pAudioClient->GetService(IID_PPV_ARGS(&pCaptureClient));
        if (FAILED(hr) || !pCaptureClient) {
            std::cerr << "WASAPI Capture: GetService(IAudioCaptureClient) failed: " << formatHResult(hr) << "\n";
            state = CaptureState::Error;
            return false;
        }

        pAudioClient->GetService(IID_PPV_ARGS(&pAudioClock));

        state = CaptureState::Initialized;
        return true;
    }

    void captureThreadLoop(CaptureCallback callback) {
        ComInitializer threadComInit(COINIT_MULTITHREADED);

        DWORD taskIndex = 0;
        HANDLE hMmcss = AvSetMmThreadCharacteristicsW(L"Capture", &taskIndex);
        if (!hMmcss) {
            hMmcss = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);
        }
        if (!hMmcss) {
            hMmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
        }

        HANDLE waitHandles[2] = { hStopEvent, hAudioEvent };

        HRESULT hrStart = pAudioClient->Start();
        if (FAILED(hrStart)) {
            std::cerr << "WASAPI Capture: IAudioClient::Start failed: " << formatHResult(hrStart) << "\n";
            if (hMmcss) AvRevertMmThreadCharacteristics(hMmcss);
            return;
        }

        std::vector<float> scratchSilence;
        std::vector<float> scratchConverted;

        while (running.load(std::memory_order_acquire)) {
            // Wait up to 100ms: if no audio is actively playing, event might not be signaled continuously
            DWORD waitRes = WaitForMultipleObjects(2, waitHandles, FALSE, 100);
            if (waitRes == WAIT_OBJECT_0) {
                // Stop event signaled
                break;
            }

            // Drain all available packets
            UINT32 packetLength = 0;
            HRESULT hr = pCaptureClient->GetNextPacketSize(&packetLength);

            while (SUCCEEDED(hr) && packetLength > 0 && running.load(std::memory_order_relaxed)) {
                BYTE* pData = nullptr;
                UINT32 numFramesRead = 0;
                DWORD flags = 0;
                UINT64 devicePosition = 0;
                UINT64 qpcPosition = 0;

                hr = pCaptureClient->GetBuffer(&pData, &numFramesRead, &flags, &devicePosition, &qpcPosition);
                if (SUCCEEDED(hr) && numFramesRead > 0) {
                    if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
                        discontinuities.fetch_add(1, std::memory_order_relaxed);
                    }

                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        silencePackets.fetch_add(1, std::memory_order_relaxed);
                        scratchSilence.assign(numFramesRead * format.channels, 0.0f);
                        callback(scratchSilence.data(), numFramesRead, format);
                    } else if (format.sampleType == SampleType::Float32) {
                        callback(reinterpret_cast<const float*>(pData), numFramesRead, format);
                    } else if (format.sampleType == SampleType::Int16) {
                        scratchConverted.resize(numFramesRead * format.channels);
                        const auto* inSamples = reinterpret_cast<const int16_t*>(pData);
                        for (size_t i = 0; i < numFramesRead * format.channels; ++i) {
                            scratchConverted[i] = static_cast<float>(inSamples[i]) / 32768.0f;
                        }
                        callback(scratchConverted.data(), numFramesRead, format);
                    } else if (format.sampleType == SampleType::Int32 || format.sampleType == SampleType::Int24In32) {
                        scratchConverted.resize(numFramesRead * format.channels);
                        const auto* inSamples = reinterpret_cast<const int32_t*>(pData);
                        for (size_t i = 0; i < numFramesRead * format.channels; ++i) {
                            scratchConverted[i] = static_cast<float>(inSamples[i]) / 2147483648.0f;
                        }
                        callback(scratchConverted.data(), numFramesRead, format);
                    }

                    pCaptureClient->ReleaseBuffer(numFramesRead);
                    framesCaptured.fetch_add(numFramesRead, std::memory_order_relaxed);
                    packetsCaptured.fetch_add(1, std::memory_order_relaxed);
                } else if (FAILED(hr)) {
                    captureErrors.fetch_add(1, std::memory_order_relaxed);
                    break;
                }

                hr = pCaptureClient->GetNextPacketSize(&packetLength);
            }
        }

        if (hMmcss) {
            AvRevertMmThreadCharacteristics(hMmcss);
        }
    }

    bool startCapture(CaptureCallback callback) {
        if (state != CaptureState::Initialized && state != CaptureState::Stopped) {
            std::cerr << "WASAPI Capture error: Client must be Initialized or Stopped to start.\n";
            return false;
        }

        if (!pAudioClient || !pCaptureClient) {
            return false;
        }

        framesCaptured.store(0);
        packetsCaptured.store(0);
        silencePackets.store(0);
        discontinuities.store(0);
        captureErrors.store(0);
        ResetEvent(hStopEvent);

        running.store(true, std::memory_order_release);
        state = CaptureState::Running;

        captureThread = std::thread(&Impl::captureThreadLoop, this, std::move(callback));
        return true;
    }
};

WasapiCapture::WasapiCapture() : impl_(std::make_unique<Impl>()) {}
WasapiCapture::~WasapiCapture() = default;
WasapiCapture::WasapiCapture(WasapiCapture&&) noexcept = default;
WasapiCapture& WasapiCapture::operator=(WasapiCapture&&) noexcept = default;

bool WasapiCapture::open(const std::string& deviceId) {
    return impl_->openDevice(deviceId);
}

bool WasapiCapture::openProcessLoopback(uint32_t excludePid) {
    return impl_->openProcessLoopbackDevice(excludePid);
}

bool WasapiCapture::initialize() {
    return impl_->initializeClient();
}

bool WasapiCapture::start(CaptureCallback callback) {
    return impl_->startCapture(std::move(callback));
}

void WasapiCapture::stop() {
    impl_->stop();
}

void WasapiCapture::close() {
    impl_->close();
}

CaptureState WasapiCapture::state() const {
    return impl_->state;
}

AudioFormat WasapiCapture::format() const {
    return impl_->format;
}

uint32_t WasapiCapture::bufferFrameCount() const {
    return impl_->bufferFrameCount;
}

uint64_t WasapiCapture::framesCaptured() const {
    return impl_->framesCaptured.load(std::memory_order_relaxed);
}

uint64_t WasapiCapture::packetsCaptured() const {
    return impl_->packetsCaptured.load(std::memory_order_relaxed);
}

uint32_t WasapiCapture::silencePackets() const {
    return impl_->silencePackets.load(std::memory_order_relaxed);
}

uint32_t WasapiCapture::discontinuities() const {
    return impl_->discontinuities.load(std::memory_order_relaxed);
}

uint32_t WasapiCapture::captureErrors() const {
    return impl_->captureErrors.load(std::memory_order_relaxed);
}

std::string WasapiCapture::deviceName() const {
    return impl_->deviceName;
}

std::string WasapiCapture::deviceId() const {
    return impl_->deviceId;
}

std::pair<uint64_t, uint64_t> WasapiCapture::getClockPosition() const {
    if (!impl_->pAudioClock) {
        return { 0, 0 };
    }
    UINT64 pos = 0;
    UINT64 qpc = 0;
    UINT64 freq = 0;
    if (SUCCEEDED(impl_->pAudioClock->GetFrequency(&freq)) &&
        SUCCEEDED(impl_->pAudioClock->GetPosition(&pos, &qpc))) {
        return { pos, freq };
    }
    return { 0, 0 };
}

} // namespace syncwave
