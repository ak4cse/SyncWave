#include "SyncController.h"
#include <algorithm>
#include <cmath>

namespace syncwave {

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
