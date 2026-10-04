#pragma once

#include "OutputLatencyModel.h"
#include "SyncError.h"
#include <vector>
#include <string>
#include <cstddef>

namespace syncwave {

enum class DriftCorrectionState {
    Disabled,
    Initializing,
    Measuring,
    Locked,
    Correcting,
    Uncertain,
    Disconnected
};

[[nodiscard]] inline std::string driftCorrectionStateToString(DriftCorrectionState state) {
    switch (state) {
        case DriftCorrectionState::Disabled:     return "Disabled";
        case DriftCorrectionState::Initializing: return "Initializing";
        case DriftCorrectionState::Measuring:    return "Measuring";
        case DriftCorrectionState::Locked:       return "Locked";
        case DriftCorrectionState::Correcting:   return "Correcting";
        case DriftCorrectionState::Uncertain:    return "Uncertain";
        case DriftCorrectionState::Disconnected: return "Disconnected";
        default:                                 return "Unknown";
    }
}

struct DriftControllerConfig {
    double deadbandMs = 1.0;            // Deadband threshold: +/- 1.0 ms
    double maxAdjustmentPpm = 100.0;     // Hard clamp: +/- 100 ppm
    double maxSlewRatePpmPerSec = 5.0;   // Slew limit: <= 5 ppm/s
    double kp = 10.0;                   // Proportional gain (ppm per ms of phase error outside deadband)
    double minObservationSec = 5.0;     // Minimum observation window before correcting
    size_t minSamples = 30;             // Minimum samples before correcting
    double minConfidence = 0.90;        // Minimum goodness-of-fit r^2
    bool enableFeedforward = true;      // Cancel observed frequency drift
};

struct DriftCorrectionOutput {
    DriftCorrectionState state = DriftCorrectionState::Disabled;
    double targetRateAdjustmentPpm = 0.0;
    double commandedPpm = 0.0;
    double proportionalTermPpm = 0.0;
    double feedforwardTermPpm = 0.0;
    bool isClamped = false;
};

class DriftController {
public:
    explicit DriftController(const DriftControllerConfig& config = {});
    ~DriftController() = default;

    void reset();
    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const { return enabled_; }

    [[nodiscard]] DriftCorrectionState state() const { return state_; }
    void setState(DriftCorrectionState s) { state_ = s; }

    [[nodiscard]] const DriftControllerConfig& config() const { return config_; }
    void setConfig(const DriftControllerConfig& config) { config_ = config; }

    // Core control algorithm
    DriftCorrectionOutput calculateCorrection(
        const SyncError& syncError,
        bool isAvailable = true);

    [[nodiscard]] double lastTargetPpm() const { return lastTargetPpm_; }

private:
    DriftControllerConfig config_;
    bool enabled_ = false;
    DriftCorrectionState state_ = DriftCorrectionState::Disabled;
    double lastTargetPpm_ = 0.0;
};

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
