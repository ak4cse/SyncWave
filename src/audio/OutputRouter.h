#pragma once

#include "DeviceOutput.h"
#include "MasterAudioBus.h"
#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <atomic>

namespace syncwave {

class OutputRouter {
public:
    OutputRouter();
    ~OutputRouter();

    OutputRouter(const OutputRouter&) = delete;
    OutputRouter& operator=(const OutputRouter&) = delete;

    // Add an audio render endpoint to the router
    bool addOutput(const AudioDevice& device);

    // Remove an endpoint by ID
    bool removeOutput(const std::string& deviceId);

    // Clear all configured outputs
    void clearOutputs();

    // Number of configured outputs
    [[nodiscard]] size_t outputCount() const;

    // Get output by index
    [[nodiscard]] DeviceOutput* getOutput(size_t index);

    // Get output by device ID
    [[nodiscard]] DeviceOutput* getOutput(const std::string& deviceId);

    // Initialize all added outputs matching master stream characteristics
    bool initializeOutputs(uint32_t masterSampleRate, uint32_t masterChannels);

    // Initialize all added outputs for testing/mocking without WASAPI hardware
    bool initializeOutputsForTesting(uint32_t masterSampleRate, uint32_t masterChannels, const std::vector<uint32_t>& deviceSampleRates = {});

    // Start WASAPI rendering across all initialized outputs
    bool startOutputs();

    // Stop playback across all outputs
    void stopOutputs();

    // Release all outputs and resources
    void closeOutputs();

    // Fan-out identical master audio frames into all active output queues
    void route(const float* sourceFrames, size_t frameCount);

    // Pull available frames from MasterAudioBus and route them to all outputs
    // Returns total frames pulled from bus
    size_t dispatch(MasterAudioBus& bus, size_t maxFrames = 1024);

    // Handle endpoint invalidation / unplug
    void onDeviceDisconnected(const std::string& deviceId);

    [[nodiscard]] bool allRunning() const;
    [[nodiscard]] bool anyRunning() const;
    [[nodiscard]] uint64_t totalFramesDistributed() const;
    [[nodiscard]] std::vector<DeviceOutputTelemetry> getOutputTelemetry() const;

private:
    mutable std::mutex outputMutex_;
    std::vector<std::unique_ptr<DeviceOutput>> outputs_;
    std::atomic<uint64_t> totalFramesDistributed_{0};
    std::vector<float> scratchDispatchBuffer_;
    uint32_t masterSampleRate_ = 48000;
    uint32_t masterChannels_ = 2;
};

} // namespace syncwave
