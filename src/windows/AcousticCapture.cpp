#include "AcousticCapture.h"
#include "ComHelper.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <functiondiscoverykeys_devpkey.h>

#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace syncwave {

static const GUID AC_SUBTYPE_IEEE_FLOAT_GUID =
    { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID AC_SUBTYPE_PCM_GUID =
    { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

struct AcousticCapture::Impl {
    ComInitializer comInit;
    ComPtr<IMMDevice> pDevice;
    ComPtr<IAudioClient> pAudioClient;
    ComPtr<IAudioCaptureClient> pCaptureClient;

    HANDLE hAudioEvent = nullptr;
    HANDLE hStopEvent = nullptr;
    std::thread captureThread;

    std::string deviceId;
    std::string deviceName;
    AudioFormat format;
    uint32_t bufferFrameCount = 0;

    std::atomic<bool> isRecording_{false};
    std::atomic<bool> isCapturingData_{false};
    std::atomic<bool> isComplete_{false};
    std::atomic<bool> firstPacket_{false};
    std::chrono::steady_clock::time_point startTime_;

    size_t targetFrames_ = 0;
    mutable std::mutex dataMutex_;
    std::vector<float> recordedSamples_;

    Impl() = default;

    ~Impl() {
        close();
    }

    void close() {
        stop();

        pCaptureClient.Reset();
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
    }

    void stop() {
        isCapturingData_.store(false, std::memory_order_release);
        if (!isRecording_.exchange(false, std::memory_order_acq_rel)) {
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
            pAudioClient->Reset();
        }
    }

    bool open(const std::string& targetId) {
        close();

        if (!comInit.succeeded()) {
            std::cerr << "AcousticCapture: COM initialization failed.\n";
            return false;
        }

        ComPtr<IMMDeviceEnumerator> pEnumerator;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(&pEnumerator)
        );

        if (FAILED(hr) || !pEnumerator) {
            std::cerr << "AcousticCapture: Failed to create MMDeviceEnumerator.\n";
            return false;
        }

        if (targetId.empty()) {
            hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDevice);
            if (FAILED(hr) || !pDevice) {
                std::cerr << "AcousticCapture: Failed to get default capture endpoint.\n";
                return false;
            }
            LPWSTR pstrId = nullptr;
            if (SUCCEEDED(pDevice->GetId(&pstrId)) && pstrId) {
                deviceId = wideToUtf8(pstrId);
                CoTaskMemFree(pstrId);
            }
        } else {
            std::wstring wid = utf8ToWide(targetId);
            hr = pEnumerator->GetDevice(wid.c_str(), &pDevice);
            if (FAILED(hr) || !pDevice) {
                std::cerr << "AcousticCapture: Failed to get device by ID (" << targetId << ").\n";
                return false;
            }
            deviceId = targetId;
        }

        // Friendly name
        ComPtr<IPropertyStore> pProps;
        deviceName = "Unknown Microphone";
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
            std::cerr << "AcousticCapture: Failed to activate IAudioClient on " << deviceName << "\n";
            return false;
        }

        return true;
    }

    bool initialize() {
        if (!pAudioClient) return false;

        WAVEFORMATEX* pwfx = nullptr;
        HRESULT hr = pAudioClient->GetMixFormat(&pwfx);
        if (FAILED(hr) || !pwfx) {
            std::cerr << "AcousticCapture: GetMixFormat failed on " << deviceName << "\n";
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
            if (InlineIsEqualGUID(pEx->SubFormat, AC_SUBTYPE_IEEE_FLOAT_GUID)) {
                format.sampleType = SampleType::Float32;
            } else if (InlineIsEqualGUID(pEx->SubFormat, AC_SUBTYPE_PCM_GUID)) {
                if (format.bitsPerSample == 16) format.sampleType = SampleType::Int16;
                else if (format.bitsPerSample == 24) format.sampleType = SampleType::Int24In32;
                else if (format.bitsPerSample == 32) format.sampleType = SampleType::Int32;
            }
        } else if (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
            format.sampleType = SampleType::Float32;
        } else if (pwfx->wFormatTag == WAVE_FORMAT_PCM) {
            if (format.bitsPerSample == 16) format.sampleType = SampleType::Int16;
            else if (format.bitsPerSample == 32) format.sampleType = SampleType::Int32;
        }

        // Shared event-driven microphone capture mode (NO loopback flag!)
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
            std::cerr << "AcousticCapture: IAudioClient::Initialize failed on " << deviceName << "\n";
            return false;
        }

        hAudioEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        hStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

        if (!hAudioEvent || !hStopEvent) {
            std::cerr << "AcousticCapture: Failed to create sync events.\n";
            return false;
        }

        hr = pAudioClient->SetEventHandle(hAudioEvent);
        if (FAILED(hr)) {
            std::cerr << "AcousticCapture: SetEventHandle failed.\n";
            return false;
        }

        pAudioClient->GetBufferSize(&bufferFrameCount);

        hr = pAudioClient->GetService(IID_PPV_ARGS(&pCaptureClient));
        if (FAILED(hr) || !pCaptureClient) {
            std::cerr << "AcousticCapture: GetService(IAudioCaptureClient) failed.\n";
            return false;
        }

        return true;
    }

    bool startRecording(size_t maxFrames) {
        if (!isRecording_.load(std::memory_order_acquire)) {
            stop();
            ResetEvent(hStopEvent);

            HRESULT hr = pAudioClient->Start();
            if (FAILED(hr)) {
                std::cerr << "AcousticCapture: IAudioClient::Start failed on " << deviceName << "\n";
                return false;
            }

            isRecording_.store(true, std::memory_order_release);
            captureThread = std::thread(&Impl::captureLoop, this);
        }

        {
            std::lock_guard<std::mutex> lock(dataMutex_);
            targetFrames_ = maxFrames;
            recordedSamples_.clear();
            recordedSamples_.reserve(maxFrames + 4800);
            isComplete_.store(false, std::memory_order_release);
        }

        firstPacket_.store(true, std::memory_order_release);
        isCapturingData_.store(true, std::memory_order_release);
        return true;
    }

    void captureLoop() {
        HANDLE waitHandles[2] = { hStopEvent, hAudioEvent };

        while (isRecording_.load(std::memory_order_acquire)) {
            DWORD waitRes = WaitForMultipleObjects(2, waitHandles, FALSE, 2000);

            if (waitRes == WAIT_OBJECT_0) {
                break; // Stop event signaled
            } else if (waitRes == WAIT_OBJECT_0 + 1) {
                // Audio packet ready
                UINT32 packetLength = 0;
                while (SUCCEEDED(pCaptureClient->GetNextPacketSize(&packetLength)) && packetLength > 0) {
                    BYTE* pData = nullptr;
                    UINT32 numFramesAvailable = 0;
                    DWORD flags = 0;
                    UINT64 devPos = 0;
                    UINT64 qpcPos = 0;

                    HRESULT hr = pCaptureClient->GetBuffer(
                        &pData,
                        &numFramesAvailable,
                        &flags,
                        &devPos,
                        &qpcPos
                    );

                    if (SUCCEEDED(hr) && pData && numFramesAvailable > 0) {
                        if (isCapturingData_.load(std::memory_order_acquire) && !isComplete_.load(std::memory_order_acquire)) {
                            if (firstPacket_.exchange(false, std::memory_order_acq_rel)) {
                                startTime_ = std::chrono::steady_clock::now();
                            }

                            const uint32_t ch = format.channels;
                            std::vector<float> monoFrames(numFramesAvailable, 0.0f);

                            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                                // Silence packet: leave as 0.0f
                            } else if (format.sampleType == SampleType::Float32) {
                                const float* src = reinterpret_cast<const float*>(pData);
                                for (size_t i = 0; i < numFramesAvailable; ++i) {
                                    float sum = 0.0f;
                                    for (uint32_t c = 0; c < ch; ++c) {
                                        sum += src[i * ch + c];
                                    }
                                    monoFrames[i] = sum / static_cast<float>(ch);
                                }
                            } else if (format.sampleType == SampleType::Int16) {
                                const int16_t* src = reinterpret_cast<const int16_t*>(pData);
                                for (size_t i = 0; i < numFramesAvailable; ++i) {
                                    float sum = 0.0f;
                                    for (uint32_t c = 0; c < ch; ++c) {
                                        sum += static_cast<float>(src[i * ch + c]) / 32768.0f;
                                    }
                                    monoFrames[i] = sum / static_cast<float>(ch);
                                }
                            } else if (format.sampleType == SampleType::Int32 || format.sampleType == SampleType::Int24In32) {
                                const int32_t* src = reinterpret_cast<const int32_t*>(pData);
                                for (size_t i = 0; i < numFramesAvailable; ++i) {
                                    float sum = 0.0f;
                                    for (uint32_t c = 0; c < ch; ++c) {
                                        sum += static_cast<float>(src[i * ch + c]) / 2147483648.0f;
                                    }
                                    monoFrames[i] = sum / static_cast<float>(ch);
                                }
                            }

                            std::lock_guard<std::mutex> lock(dataMutex_);
                            recordedSamples_.insert(recordedSamples_.end(), monoFrames.begin(), monoFrames.end());
                            if (recordedSamples_.size() >= targetFrames_) {
                                isComplete_.store(true, std::memory_order_release);
                                isCapturingData_.store(false, std::memory_order_release);
                            }
                        }

                        pCaptureClient->ReleaseBuffer(numFramesAvailable);
                    } else if (FAILED(hr)) {
                        break;
                    }
                }
            } else if (waitRes == WAIT_TIMEOUT) {
                // Timeout
                continue;
            } else {
                break;
            }
        }
    }
};

AcousticCapture::AcousticCapture()
    : impl_(std::make_unique<Impl>()) {}

AcousticCapture::~AcousticCapture() = default;
AcousticCapture::AcousticCapture(AcousticCapture&&) noexcept = default;
AcousticCapture& AcousticCapture::operator=(AcousticCapture&&) noexcept = default;

bool AcousticCapture::open(const std::string& deviceId) {
    return impl_->open(deviceId);
}

bool AcousticCapture::initialize() {
    return impl_->initialize();
}

bool AcousticCapture::startRecording(size_t maxFrames) {
    return impl_->startRecording(maxFrames);
}

void AcousticCapture::stopRecording() {
    impl_->isCapturingData_.store(false, std::memory_order_release);
}

void AcousticCapture::close() {
    impl_->close();
}

bool AcousticCapture::isRecording() const {
    return impl_->isCapturingData_.load(std::memory_order_acquire);
}

bool AcousticCapture::isComplete() const {
    return impl_->isComplete_.load(std::memory_order_acquire);
}

uint32_t AcousticCapture::sampleRate() const {
    return impl_->format.sampleRate;
}

uint32_t AcousticCapture::channels() const {
    return impl_->format.channels;
}

std::string AcousticCapture::deviceName() const {
    return impl_->deviceName;
}

std::string AcousticCapture::deviceId() const {
    return impl_->deviceId;
}

std::chrono::steady_clock::time_point AcousticCapture::startTime() const {
    return impl_->startTime_;
}

std::vector<float> AcousticCapture::getRecordedSamples() const {
    std::lock_guard<std::mutex> lock(impl_->dataMutex_);
    return impl_->recordedSamples_;
}

} // namespace syncwave
