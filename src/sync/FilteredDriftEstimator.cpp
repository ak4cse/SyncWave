#include "FilteredDriftEstimator.h"
#include <algorithm>
#include <cmath>

namespace syncwave {

FilteredDriftEstimator::FilteredDriftEstimator(
    double minObservationSec,
    size_t minSamples,
    double maxAllowedPpm,
    double emaAlphaDrift,
    double emaAlphaPhase)
    : minObservationSec_(minObservationSec)
    , minSamples_(minSamples)
    , maxAllowedPpm_(maxAllowedPpm)
    , emaAlphaDrift_(emaAlphaDrift)
    , emaAlphaPhase_(emaAlphaPhase)
{
}

void FilteredDriftEstimator::reset() {
    hasBaseline_ = false;
    filteredDriftPpm_ = 0.0;
    filteredPhaseErrorSec_ = 0.0;
    lastValidDriftPpm_ = 0.0;
    validUpdateCount_ = 0;
}

FilteredDriftResult FilteredDriftEstimator::update(
    const DeviceClock& clock,
    double targetPlayheadSec,
    double outputPlayheadSec,
    double windowSec)
{
    FilteredDriftResult result;
    result.rawPhaseErrorSec = targetPlayheadSec - outputPlayheadSec;
    result.rawPhaseErrorMs = result.rawPhaseErrorSec * 1000.0;

    ClockRateEstimate rateEstimate = clock.estimateRateOverWindow(windowSec);

    result.sampleCount = rateEstimate.sampleCount;
    result.observationDurationSec = rateEstimate.measurementDurationSec;

    // Check if we have sufficient samples and observation duration
    bool sufficientData = rateEstimate.isValid &&
                          (rateEstimate.sampleCount >= minSamples_) &&
                          (rateEstimate.measurementDurationSec >= minObservationSec_);

    if (!sufficientData) {
        // Not enough data yet to establish or update reliable drift estimation
        if (hasBaseline_) {
            // Still update phase error filtering if baseline exists
            filteredPhaseErrorSec_ = emaAlphaPhase_ * result.rawPhaseErrorSec +
                                     (1.0 - emaAlphaPhase_) * filteredPhaseErrorSec_;
            result.filteredPhaseErrorSec = filteredPhaseErrorSec_;
            result.filteredPhaseErrorMs = filteredPhaseErrorSec_ * 1000.0;
            result.filteredDriftPpm = filteredDriftPpm_;
            result.rawDriftPpm = rateEstimate.isValid ? rateEstimate.rateErrorPpm : lastValidDriftPpm_;
            result.confidence = 0.5 * (rateEstimate.sampleCount / static_cast<double>(minSamples_));
            result.isValid = true;
        } else {
            result.filteredPhaseErrorSec = result.rawPhaseErrorSec;
            result.filteredPhaseErrorMs = result.rawPhaseErrorMs;
            result.filteredDriftPpm = 0.0;
            result.rawDriftPpm = 0.0;
            result.confidence = 0.0;
            result.isValid = false;
        }
        return result;
    }

    result.rawDriftPpm = rateEstimate.rateErrorPpm;

    // Outlier rejection checks:
    // 1. Extreme absolute PPM deviation
    // 2. Linear regression goodness of fit (r^2 < minConfidence_)
    // 3. Excessive sudden jump from last verified drift (if baseline established)
    bool isOutlier = false;
    if (std::abs(rateEstimate.rateErrorPpm) > maxAllowedPpm_) {
        isOutlier = true;
    }
    if (rateEstimate.rSquared < minConfidence_) {
        isOutlier = true;
    }
    if (hasBaseline_ && std::abs(rateEstimate.rateErrorPpm - lastValidDriftPpm_) > (maxAllowedPpm_ * 0.75)) {
        isOutlier = true;
    }

    if (isOutlier) {
        result.isOutlierRejected = true;
        result.filteredDriftPpm = filteredDriftPpm_;
        result.filteredPhaseErrorSec = filteredPhaseErrorSec_;
        result.filteredPhaseErrorMs = filteredPhaseErrorSec_ * 1000.0;
        result.confidence = 0.2; // low confidence on outlier
        result.isValid = hasBaseline_;
        return result;
    }

    // Valid sample: apply Exponential Moving Average (EMA)
    if (!hasBaseline_) {
        filteredDriftPpm_ = result.rawDriftPpm;
        filteredPhaseErrorSec_ = result.rawPhaseErrorSec;
        hasBaseline_ = true;
    } else {
        filteredDriftPpm_ = emaAlphaDrift_ * result.rawDriftPpm +
                            (1.0 - emaAlphaDrift_) * filteredDriftPpm_;
        filteredPhaseErrorSec_ = emaAlphaPhase_ * result.rawPhaseErrorSec +
                                 (1.0 - emaAlphaPhase_) * filteredPhaseErrorSec_;
    }

    lastValidDriftPpm_ = result.rawDriftPpm;
    validUpdateCount_++;

    result.filteredDriftPpm = filteredDriftPpm_;
    result.filteredPhaseErrorSec = filteredPhaseErrorSec_;
    result.filteredPhaseErrorMs = filteredPhaseErrorSec_ * 1000.0;

    // Confidence metric based on r^2 and observation duration relative to min observation requirement
    double durationRatio = std::min(1.0, rateEstimate.measurementDurationSec / std::max(minObservationSec_, 1.0));
    result.confidence = std::clamp(rateEstimate.rSquared * durationRatio, 0.0, 1.0);
    result.isValid = true;

    return result;
}

SyncError FilteredDriftEstimator::updateSyncError(
    const std::string& deviceId,
    const std::string& deviceName,
    const DeviceClock& clock,
    double masterTimelineSec,
    uint64_t masterTimelineFrames,
    double targetPlayheadSec,
    double outputPlayheadSec,
    uint32_t masterSampleRate,
    double windowSec)
{
    FilteredDriftResult res = update(clock, targetPlayheadSec, outputPlayheadSec, windowSec);

    SyncError error;
    error.deviceId = deviceId;
    error.deviceName = deviceName;
    error.masterTimelineSec = masterTimelineSec;
    error.masterTimelineFrames = masterTimelineFrames;
    error.outputPlayheadSec = outputPlayheadSec;
    error.outputPlayheadFrames = static_cast<uint64_t>(std::max(0.0, outputPlayheadSec * masterSampleRate));
    error.targetPlayheadSec = targetPlayheadSec;
    error.targetPlayheadFrames = static_cast<uint64_t>(std::max(0.0, targetPlayheadSec * masterSampleRate));

    error.phaseErrorSec = res.rawPhaseErrorSec;
    error.phaseErrorMs = res.rawPhaseErrorMs;
    error.phaseErrorFrames = static_cast<int64_t>(res.rawPhaseErrorSec * masterSampleRate);

    error.filteredPhaseErrorSec = res.filteredPhaseErrorSec;
    error.filteredPhaseErrorMs = res.filteredPhaseErrorMs;

    error.rawDriftPpm = res.rawDriftPpm;
    error.filteredDriftPpm = res.filteredDriftPpm;

    error.confidence = res.confidence;
    error.observationDurationSec = res.observationDurationSec;
    error.isValid = res.isValid;

    return error;
}

} // namespace syncwave
