#include "SyncController.h"
#include <algorithm>
#include <cmath>

namespace syncwave {

DriftController::DriftController(const DriftControllerConfig& config)
    : config_(config), enabled_(false), state_(DriftCorrectionState::Disabled) {}

void DriftController::reset() {
    state_ = enabled_ ? DriftCorrectionState::Initializing : DriftCorrectionState::Disabled;
    lastTargetPpm_ = 0.0;
}

void DriftController::setEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        state_ = DriftCorrectionState::Disabled;
        lastTargetPpm_ = 0.0;
    } else {
        if (state_ == DriftCorrectionState::Disabled) {
            state_ = DriftCorrectionState::Initializing;
        }
    }
}

DriftCorrectionOutput DriftController::calculateCorrection(
    const SyncError& syncError,
    bool isAvailable)
{
    DriftCorrectionOutput out;

    if (!enabled_) {
        state_ = DriftCorrectionState::Disabled;
        out.state = state_;
        out.targetRateAdjustmentPpm = 0.0;
        lastTargetPpm_ = 0.0;
        return out;
    }

    if (!isAvailable) {
        state_ = DriftCorrectionState::Disconnected;
        out.state = state_;
        out.targetRateAdjustmentPpm = 0.0;
        lastTargetPpm_ = 0.0;
        return out;
    }

    // Check observation duration and sample validity
    if (syncError.observationDurationSec < config_.minObservationSec ||
        syncError.confidence <= 0.0 ||
        !syncError.isValid)
    {
        state_ = (syncError.observationDurationSec <= 0.1) 
                 ? DriftCorrectionState::Initializing 
                 : DriftCorrectionState::Measuring;
        out.state = state_;
        out.targetRateAdjustmentPpm = 0.0;
        lastTargetPpm_ = 0.0;
        return out;
    }

    // Check confidence threshold (reject noisy / non-linear regressions)
    if (syncError.confidence < config_.minConfidence) {
        state_ = DriftCorrectionState::Uncertain;
        out.state = state_;
        // In Uncertain state, hold last safe target or decay gently towards 0
        out.targetRateAdjustmentPpm = lastTargetPpm_ * 0.95;
        lastTargetPpm_ = out.targetRateAdjustmentPpm;
        return out;
    }

    // Valid measurement: compute control terms
    // 1. Feedforward drift cancellation:
    // If output clock runs slow (filteredDriftPpm < 0), resampler needs positive adjustment to match speed.
    double feedforward = 0.0;
    if (config_.enableFeedforward) {
        feedforward = -syncError.filteredDriftPpm;
    }

    // 2. Proportional feedback on phase error outside deadband:
    // phaseErrorMs = targetPlayhead - outputPlayhead
    // If phaseErrorMs > +deadband: output is lagging, needs positive adjustment (speedup)
    // If phaseErrorMs < -deadband: output is leading, needs negative adjustment (slowdown)
    double phaseError = syncError.filteredPhaseErrorMs;
    double excessError = 0.0;

    if (phaseError > config_.deadbandMs) {
        excessError = phaseError - config_.deadbandMs;
    } else if (phaseError < -config_.deadbandMs) {
        excessError = phaseError + config_.deadbandMs;
    } else {
        excessError = 0.0; // inside deadband
    }

    double feedback = config_.kp * excessError;

    // Combine terms
    double commanded = feedforward + feedback;

    // State assignment
    if (std::abs(phaseError) <= config_.deadbandMs) {
        state_ = DriftCorrectionState::Locked;
    } else {
        state_ = DriftCorrectionState::Correcting;
    }

    // Hard clamp to +/- maxAdjustmentPpm
    double clamped = std::clamp(commanded, -config_.maxAdjustmentPpm, config_.maxAdjustmentPpm);
    bool isClamped = (std::abs(commanded) > config_.maxAdjustmentPpm);

    out.state = state_;
    out.commandedPpm = commanded;
    out.proportionalTermPpm = feedback;
    out.feedforwardTermPpm = feedforward;
    out.targetRateAdjustmentPpm = clamped;
    out.isClamped = isClamped;

    lastTargetPpm_ = clamped;
    return out;
}

double SyncController::calculateTargetLatency(const std::vector<OutputLatencyModel>& models) {
    if (models.empty()) {
        return 0.0;
    }
    double maxLatency = 0.0;
    for (const auto& m : models) {
        maxLatency = std::max(maxLatency, m.effectiveLatencyMs);
    }
    return maxLatency;
}

double SyncController::calculateDeviceDelay(double targetLatency, const OutputLatencyModel& model) {
    return std::max(0.0, targetLatency - model.effectiveLatencyMs);
}

SyncPlan SyncController::computeSoftwareAlignmentPlan(const std::vector<OutputLatencyModel>& models) {
    SyncPlan plan;
    if (models.empty()) {
        plan.isValid = false;
        plan.syncState = SyncState::Disabled;
        return plan;
    }

    plan.targetLatencyMs = calculateTargetLatency(models);
    plan.calculatedDelaysMs.resize(models.size(), 0.0);
    plan.calculatedDelaysFrames.resize(models.size(), 0);

    bool hasPhysicalOffset = false;
    for (size_t i = 0; i < models.size(); ++i) {
        double delayMs = calculateDeviceDelay(plan.targetLatencyMs, models[i]);
        plan.calculatedDelaysMs[i] = delayMs;

        uint32_t sRate = models[i].sampleRate > 0 ? models[i].sampleRate : 48000;
        size_t frames = static_cast<size_t>(std::round((delayMs * static_cast<double>(sRate)) / 1000.0));
        plan.calculatedDelaysFrames[i] = frames;

        if (std::abs(models[i].optionalCalibrationOffsetMs) > 1e-6) {
            hasPhysicalOffset = true;
        }
    }

    plan.syncState = hasPhysicalOffset ? SyncState::PhysicallyCalibrated : SyncState::SoftwareCalibrated;
    plan.isValid = true;
    return plan;
}

SyncPlan SyncController::computeManualPlan(
    const std::vector<OutputLatencyModel>& models, 
    const std::vector<double>& manualDelaysMs) {

    SyncPlan plan;
    if (models.empty()) {
        plan.isValid = false;
        plan.syncState = SyncState::Disabled;
        return plan;
    }

    plan.calculatedDelaysMs.resize(models.size(), 0.0);
    plan.calculatedDelaysFrames.resize(models.size(), 0);

    double maxEffective = 0.0;
    for (size_t i = 0; i < models.size(); ++i) {
        double dMs = (i < manualDelaysMs.size()) ? std::max(0.0, manualDelaysMs[i]) : 0.0;
        plan.calculatedDelaysMs[i] = dMs;

        uint32_t sRate = models[i].sampleRate > 0 ? models[i].sampleRate : 48000;
        size_t frames = static_cast<size_t>(std::round((dMs * static_cast<double>(sRate)) / 1000.0));
        plan.calculatedDelaysFrames[i] = frames;

        maxEffective = std::max(maxEffective, models[i].effectiveLatencyMs + dMs);
    }

    plan.targetLatencyMs = maxEffective;
    plan.syncState = SyncState::Manual;
    plan.isValid = true;
    return plan;
}

} // namespace syncwave
