#pragma once

#include "AudioFormat.h"
#include "ToneGenerator.h"
#include "../windows/DeviceManager.h"
#include <string>
#include <memory>
#include <utility>

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
    uint32_t bufferFrameCount = 0;
    uint64_t framesRendered = 0;
    uint32_t underruns = 0;
    uint64_t clockPosition = 0;
    uint64_t clockFrequency = 0;
    bool isRunning = false;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&&) noexcept;
    AudioEngine& operator=(AudioEngine&&) noexcept;

    // Start tone playback through specified endpoint
    bool startTone(const AudioDevice& device, const ToneParameters& params);
    bool startTone(const std::string& deviceId, const ToneParameters& params);

    // Stop active audio stream
    void stop();

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] EngineDiagnostics getDiagnostics() const;

private:
    std::unique_ptr<WasapiOutput> output_;
    ToneGenerator toneGen_;
    ToneParameters currentParams_;
};

} // namespace syncwave
