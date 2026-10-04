#pragma once

#include "OutputLatencyModel.h"
#include <vector>
#include <string>
#include <cstddef>

namespace syncwave {

struct SyncPlan {
    double targetLatencyMs = 0.0;
    std::vector<double> calculatedDelaysMs;
    std::vector<size_t> calculatedDelaysFrames;
    SyncState syncState = SyncState::Disabled;
    bool isValid = false;
};

class SyncController {
public:
    SyncController() = default;
    ~SyncController() = default;

    // Fundamental synchronization rule: target latency is the maximum effective latency across all endpoints
    [[nodiscard]] static double calculateTargetLatency(const std::vector<OutputLatencyModel>& models);

    // Fundamental per-device delay: max(0.0, targetLatency - deviceEffectiveLatency)
    [[nodiscard]] static double calculateDeviceDelay(double targetLatency, const OutputLatencyModel& model);

    // Compute automatic software-domain alignment plan across a set of endpoint latency models
    [[nodiscard]] static SyncPlan computeSoftwareAlignmentPlan(const std::vector<OutputLatencyModel>& models);

    // Compute plan for explicit manual delays (clamping any negative values to 0.0)
    [[nodiscard]] static SyncPlan computeManualPlan(
        const std::vector<OutputLatencyModel>& models, 
        const std::vector<double>& manualDelaysMs);

    // State query
    [[nodiscard]] SyncState syncState() const { return state_; }
    void setSyncState(SyncState state) { state_ = state; }

private:
    SyncState state_ = SyncState::Disabled;
};

} // namespace syncwave
