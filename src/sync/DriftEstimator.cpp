#include "DriftEstimator.h"
#include <algorithm>
#include <cmath>

namespace syncwave {

double DriftEstimator::calculatePpm(double observedRate, double nominalRate) {
    if (nominalRate <= 0.0) {
        return 0.0;
    }
    return ((observedRate / nominalRate) - 1.0) * 1e6;
}

double DriftEstimator::calculateRelativePpm(double rateA, double nominalA, double rateB, double nominalB) {
    if (nominalA <= 0.0 || nominalB <= 0.0 || rateB <= 0.0) {
        return 0.0;
    }
    const double normA = rateA / nominalA;
    const double normB = rateB / nominalB;
    return ((normA / normB) - 1.0) * 1e6;
}

double DriftEstimator::calculateOffset(double posSecA, double posSecB) {
    return posSecA - posSecB;
}

double DriftEstimator::calculateAccumulatedDrift(double initialOffsetSec, double finalOffsetSec) {
    return finalOffsetSec - initialOffsetSec;
}

double DriftEstimator::calculateDriftRateFromDelta(double deltaOffsetSec, double elapsedSec) {
    if (elapsedSec <= 0.0) {
        return 0.0;
    }
    return (deltaOffsetSec / elapsedSec) * 1e6;
}

PairwiseDriftEstimate DriftEstimator::estimate(
    const DeviceClock& clockA,
    const DeviceClock& clockB,
    const std::string& nameA,
    const std::string& nameB) {
    return estimateOverWindow(clockA, clockB, 0.0, nameA, nameB);
}

PairwiseDriftEstimate DriftEstimator::estimateOverWindow(
    const DeviceClock& clockA,
    const DeviceClock& clockB,
    double windowSec,
    const std::string& nameA,
    const std::string& nameB) {

    PairwiseDriftEstimate result;
    result.deviceIdA = clockA.deviceId();
    result.deviceIdB = clockB.deviceId();
    result.deviceNameA = nameA.empty() ? clockA.deviceId() : nameA;
    result.deviceNameB = nameB.empty() ? clockB.deviceId() : nameB;
    result.nominalRateA = clockA.nominalSampleRate();
    result.nominalRateB = clockB.nominalSampleRate();
    result.windowRequestedSec = windowSec;

    const auto estA = clockA.estimateRateOverWindow(windowSec);
    const auto estB = clockB.estimateRateOverWindow(windowSec);

    result.sampleCountA = estA.sampleCount;
    result.sampleCountB = estB.sampleCount;

    if (!estA.isValid || !estB.isValid) {
        result.isValid = false;
        return result;
    }

    result.estimatedRateA = estA.estimatedRate;
    result.estimatedRateB = estB.estimatedRate;
    result.measurementDurationSec = std::min(estA.measurementDurationSec, estB.measurementDurationSec);
    result.confidence = estA.rSquared * estB.rSquared;

    const double normA = (result.nominalRateA > 0) ? (estA.estimatedRate / result.nominalRateA) : 1.0;
    const double normB = (result.nominalRateB > 0) ? (estB.estimatedRate / result.nominalRateB) : 1.0;

    if (normB > 1e-9) {
        result.rateRatio = normA / normB;
        result.driftRatePpm = (result.rateRatio - 1.0) * 1e6;
    } else {
        result.rateRatio = 1.0;
        result.driftRatePpm = 0.0;
    }
    result.relativeDriftPpm = result.driftRatePpm;

    // Calculate instantaneous playhead offset (seconds)
    const auto latestA = clockA.latestSample();
    const auto latestB = clockB.latestSample();
    if (latestA && latestB) {
        result.instantaneousOffsetSec = calculateOffset(latestA->positionSeconds(), latestB->positionSeconds());
        result.relativeOffsetSec = result.instantaneousOffsetSec;
    }

    // Calculate initial playhead offset and accumulated drift across the history / window
    const auto firstA = clockA.firstSample();
    const auto firstB = clockB.firstSample();
    if (firstA && firstB) {
        result.initialOffsetSec = calculateOffset(firstA->positionSeconds(), firstB->positionSeconds());
        result.accumulatedDriftSec = calculateAccumulatedDrift(result.initialOffsetSec, result.instantaneousOffsetSec);
    }

    result.isValid = true;
    return result;
}

} // namespace syncwave
