#pragma once

#include "AudioFormat.h"
#include "ToneGenerator.h"
#include "MasterAudioBus.h"
#include "../dsp/Resampler.h"
#include "../windows/DeviceManager.h"
#include <string>
#include <memory>
#include <thread>
#include <atomic>

namespace syncwave {

class WasapiOutput;
class WasapiCapture;

struct ToneParameters {
    double frequencyHz = 440.0;
    double volume = 0.25;
    double durationSec = 0.0; // 0 means continuous
};

struct EngineDiagnostics {
    std::string deviceName;
    std::string deviceId;
    std::string captureDeviceName;
    std::string captureDeviceId;
    AudioFormat format;
    AudioFormat captureFormat;
    AudioFormat masterFormat;
    uint32_t bufferFrameCount = 0;
    uint64_t framesRendered = 0;
    uint32_t outputUnderruns = 0;
    uint64_t clockPosition = 0;
    uint64_t clockFrequency = 0;

    uint64_t framesCaptured = 0;
    uint64_t packetsCaptured = 0;
    uint32_t silencePackets = 0;
    uint32_t discontinuities = 0;
    uint32_t captureErrors = 0;

    size_t busCapacityFrames = 0;
    size_t busAvailableFrames = 0;
    uint64_t busFramesWritten = 0;
    uint64_t busFramesRead = 0;
    uint64_t busUnderruns = 0;
    uint64_t busOverruns = 0;

    bool isRunning = false;
    bool isCaptureMode = false;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&&) = delete;
    AudioEngine& operator=(AudioEngine&&) = delete;

    // Start tone playback routed through MasterAudioBus into target endpoint
    bool startTone(const AudioDevice& device, const ToneParameters& params);
    bool startTone(const std::string& deviceId, const ToneParameters& params);

    // Start system audio loopback capture routed through MasterAudioBus into output endpoint
    bool startCapture(const std::string& captureDeviceId = "", const std::string& outputDeviceId = "");
    bool startCapture(const AudioDevice& captureDevice, const AudioDevice& outputDevice);

    // Stop playback/capture and threads
    void stop();

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] EngineDiagnostics getDiagnostics() const;
    [[nodiscard]] MasterAudioBus& masterBus() { return masterBus_; }
    [[nodiscard]] WasapiCapture* capture() { return capture_.get(); }
    [[nodiscard]] WasapiOutput* output() { return output_.get(); }

private:
    void producerLoop();

    std::unique_ptr<WasapiOutput> output_;
    std::unique_ptr<WasapiCapture> capture_;
    ToneGenerator toneGen_;
    MasterAudioBus masterBus_;
    Resampler resampler_;
    ToneParameters currentParams_;

    std::thread producerThread_;
    std::atomic<bool> producerRunning_{false};
    bool isCaptureMode_ = false;
    EngineDiagnostics lastDiag_{};
};

} // namespace syncwave
