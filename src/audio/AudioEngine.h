#pragma once

#include "AudioFormat.h"
#include "ToneGenerator.h"
#include "MasterAudioBus.h"
#include "../windows/DeviceManager.h"
#include <string>
#include <memory>
#include <thread>
#include <atomic>

namespace syncwave {

class WasapiOutput;

struct ToneParameters {
    double frequencyHz = 440.0;
    double volume = 0.25;
    double durationSec = 0.0; // 0 means continuous
};

struct EngineDiagnostics {
    std::string deviceName;
    std::string deviceId;
    AudioFormat format;
    AudioFormat masterFormat;
    uint32_t bufferFrameCount = 0;
    uint64_t framesRendered = 0;
    uint32_t outputUnderruns = 0;
    uint64_t clockPosition = 0;
    uint64_t clockFrequency = 0;

    size_t busCapacityFrames = 0;
    size_t busAvailableFrames = 0;
    uint64_t busFramesWritten = 0;
    uint64_t busFramesRead = 0;
    uint64_t busUnderruns = 0;
    uint64_t busOverruns = 0;

    bool isRunning = false;
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

    // Stop playback and producer thread
    void stop();

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] EngineDiagnostics getDiagnostics() const;
    [[nodiscard]] MasterAudioBus& masterBus() { return masterBus_; }

private:
    void producerLoop();

    std::unique_ptr<WasapiOutput> output_;
    ToneGenerator toneGen_;
    MasterAudioBus masterBus_;
    ToneParameters currentParams_;

    std::thread producerThread_;
    std::atomic<bool> producerRunning_{false};
    EngineDiagnostics lastDiag_{};
};

} // namespace syncwave
