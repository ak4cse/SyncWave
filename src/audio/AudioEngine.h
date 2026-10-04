#pragma once

#include "AudioFormat.h"
#include "ToneGenerator.h"
#include "MasterAudioBus.h"
#include "OutputRouter.h"
#include "../windows/DeviceManager.h"
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>

namespace syncwave {

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

    uint64_t routerFramesDistributed = 0;
    std::vector<DeviceOutputTelemetry> outputs;
    std::vector<PairwiseDriftEstimate> pairwiseDrift;

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

    // Manually trigger a clock sample across all active outputs
    void sampleClocks();

    // Start tone playback routed through MasterAudioBus into target endpoint(s)
    bool startTone(const AudioDevice& device, const ToneParameters& params);
    bool startTone(const std::string& deviceId, const ToneParameters& params);
    bool startTone(const std::vector<AudioDevice>& devices, const ToneParameters& params);
    bool startTone(const std::vector<std::string>& deviceIds, const ToneParameters& params);

    // Start system audio loopback capture routed through MasterAudioBus into output endpoint(s)
    bool startCapture(const std::string& captureDeviceId = "", const std::string& outputDeviceId = "");
    bool startCapture(const AudioDevice& captureDevice, const AudioDevice& outputDevice);
    bool startCapture(const std::string& captureDeviceId, const std::vector<std::string>& outputDeviceIds);
    bool startCapture(const AudioDevice& captureDevice, const std::vector<AudioDevice>& outputDevices);

    // Notify engine of endpoint disconnect / unplug
    void onDeviceDisconnected(const std::string& deviceId);

    // Stop playback/capture and threads
    void stop();

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] EngineDiagnostics getDiagnostics() const;
    [[nodiscard]] MasterAudioBus& masterBus() { return masterBus_; }
    [[nodiscard]] OutputRouter& outputRouter() { return router_; }
    [[nodiscard]] WasapiCapture* capture() { return capture_.get(); }

private:
    void producerLoop();

    std::unique_ptr<WasapiCapture> capture_;
    ToneGenerator toneGen_;
    MasterAudioBus masterBus_;
    OutputRouter router_;
    ToneParameters currentParams_;

    std::thread producerThread_;
    std::atomic<bool> producerRunning_{false};
    bool isCaptureMode_ = false;
    EngineDiagnostics lastDiag_{};
};

} // namespace syncwave
