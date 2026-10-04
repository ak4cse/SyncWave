#pragma once

#include "AudioFormat.h"
#include "RingBuffer.h"
#include "../dsp/Resampler.h"
#include "../windows/DeviceManager.h"
#include "../windows/WasapiOutput.h"
#include "../sync/DeviceClock.h"
#include <string>
#include <memory>
#include <atomic>
#include <utility>

namespace syncwave {

struct DeviceOutputTelemetry {
    std::string deviceId;
    std::string deviceName;
    OutputState state = OutputState::Uninitialized;
    AudioFormat format;
    uint32_t masterSampleRate = 48000;
    uint32_t bufferFrameCount = 0;
    size_t queueCapacity = 0;
    size_t queueAvailable = 0;
    uint64_t framesRouted = 0;
    uint64_t framesConsumed = 0;
    uint64_t framesResampled = 0;
    uint64_t framesSubmitted = 0;
    uint64_t queueUnderruns = 0;
    uint64_t queueOverruns = 0;
    uint32_t wasapiUnderruns = 0;
    uint64_t clockPosition = 0;
    uint64_t clockFrequency = 0;
    uint32_t currentPadding = 0;
    double streamLatencyMs = 0.0;
    double estimatedClockRateHz = 0.0;
    double rateErrorPpm = 0.0;
    double measurementDurationSec = 0.0;
    size_t clockSampleCount = 0;
    uint64_t estimatedAppPlayheadFrames = 0;
    double estimatedAppPlayheadSec = 0.0;
    double wasapiClockPlayheadSec = 0.0;
    double playheadDiscrepancyMs = 0.0;
    uint64_t masterTimelineFrames = 0;
    double masterTimelineSec = 0.0;
    bool isAvailable = true;
};

class DeviceOutput {
public:
    explicit DeviceOutput(const AudioDevice& device);
    ~DeviceOutput();

    DeviceOutput(const DeviceOutput&) = delete;
    DeviceOutput& operator=(const DeviceOutput&) = delete;
    DeviceOutput(DeviceOutput&&) = delete;
    DeviceOutput& operator=(DeviceOutput&&) = delete;

    // Initialize WASAPI output, queue, and resampler matching master stream parameters
    bool initialize(uint32_t masterSampleRate, uint32_t masterChannels);

    // Initialize in-memory queue and resampler without binding WASAPI hardware (for testing/mocking)
    bool initializeForTesting(uint32_t masterSampleRate, uint32_t masterChannels, uint32_t deviceSampleRate = 48000);

    // Start WASAPI playback thread pulling from queue
    bool start();

    // Stop playback thread and halt stream
    void stop();

    // Release all resources
    void close();

    // Push master frames into the output queue (returns actual frames written)
    size_t push(const float* sourceFrames, size_t frameCount);

    [[nodiscard]] const AudioDevice& device() const { return device_; }
    [[nodiscard]] const std::string& deviceId() const { return device_.id; }
    [[nodiscard]] const std::string& deviceName() const { return device_.name; }
    [[nodiscard]] OutputState state() const;
    [[nodiscard]] AudioFormat format() const;
    [[nodiscard]] bool isAvailable() const { return isAvailable_.load(std::memory_order_relaxed); }
    void markUnavailable() { isAvailable_.store(false, std::memory_order_release); }

    [[nodiscard]] RingBuffer* queue() { return queue_.get(); }
    [[nodiscard]] const RingBuffer* queue() const { return queue_.get(); }
    [[nodiscard]] Resampler* resampler() { return resampler_.get(); }

    // Sample WASAPI clock, stream latency, and padding into DeviceClock
    DeviceClockSample sampleClock(std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now());

    [[nodiscard]] DeviceClock& clock() { return clock_; }
    [[nodiscard]] const DeviceClock& clock() const { return clock_; }

    // Playhead estimation methods
    [[nodiscard]] uint64_t estimatedAppPlayheadFrames() const;
    [[nodiscard]] double estimatedAppPlayheadSeconds() const;
    [[nodiscard]] double wasapiClockPlayheadSeconds() const;
    [[nodiscard]] double playheadDiscrepancyMs() const;
    [[nodiscard]] uint64_t masterTimelineFrames() const;
    [[nodiscard]] double masterTimelineSeconds() const;

    [[nodiscard]] DeviceOutputTelemetry getTelemetry() const;

private:
    AudioDevice device_;
    AudioFormat format_;
    uint32_t masterSampleRate_ = 48000;
    uint32_t masterChannels_ = 2;

    std::unique_ptr<WasapiOutput> wasapiOutput_;
    std::unique_ptr<RingBuffer> queue_;
    std::unique_ptr<Resampler> resampler_;
    DeviceClock clock_;

    std::atomic<uint64_t> framesRouted_{0};
    std::atomic<uint64_t> framesConsumed_{0};
    std::atomic<uint64_t> framesResampled_{0};
    std::atomic<uint64_t> queueUnderruns_{0};
    std::atomic<uint64_t> queueOverruns_{0};
    std::atomic<bool> isAvailable_{true};
};

} // namespace syncwave
