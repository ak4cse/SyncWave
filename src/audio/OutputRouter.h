#pragma once

#include "DeviceOutput.h"
#include "MasterAudioBus.h"
#include "../sync/DriftEstimator.h"
#include "../sync/SyncController.h"
#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <atomic>
#include <chrono>

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

    // Handle endpoint reconnection
    bool onDeviceReconnected(const std::string& deviceId);

    // Sample clocks across all active outputs simultaneously
    void sampleAllClocks(std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now());

    // Pairwise clock rate and drift estimation between all active output pairs
    [[nodiscard]] std::vector<PairwiseDriftEstimate> getPairwiseDriftEstimates() const;

    // Pairwise clock rate and drift estimation over a specific trailing time window
    [[nodiscard]] std::vector<PairwiseDriftEstimate> getPairwiseDriftEstimatesOverWindow(double windowSec) const;

    // Latency and synchronization methods
    [[nodiscard]] std::vector<OutputLatencyModel> getLatencyModels() const;
    void setDeviceDelayMs(size_t index, double delayMs);
    void setDeviceDelayFrames(size_t index, size_t frames);
    void setDeviceCalibrationOffsetMs(size_t index, double offsetMs);
    void setSyncStateAll(SyncState state);
    void applySyncPlan(const SyncPlan& plan);

    // Micro-resampling drift correction methods
    void setDriftCorrectionEnabled(bool enable);
    [[nodiscard]] bool isDriftCorrectionEnabled() const;
    void updateDriftCorrection();
    void setTargetLatencyMs(double targetLatencyMs);
    [[nodiscard]] double targetLatencyMs() const;

    [[nodiscard]] bool allRunning() const;
    [[nodiscard]] bool anyRunning() const;
    [[nodiscard]] uint64_t totalFramesDistributed() const;
    [[nodiscard]] std::vector<DeviceOutputTelemetry> getOutputTelemetry() const;

private:
    void updateDriftCorrectionLocked();

    mutable std::mutex outputMutex_;
    std::vector<std::unique_ptr<DeviceOutput>> outputs_;
    std::atomic<uint64_t> totalFramesDistributed_{0};
    std::vector<float> scratchDispatchBuffer_;
    uint32_t masterSampleRate_ = 48000;
    uint32_t masterChannels_ = 2;
    std::atomic<bool> driftCorrectionEnabled_{false};
    double targetLatencyMs_ = 0.0;
};

} // namespace syncwave
