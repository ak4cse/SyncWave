#pragma once

#include "DeviceClock.h"
#include <string>

namespace syncwave {

struct PairwiseDriftEstimate {
    std::string deviceIdA;
    std::string deviceIdB;
    std::string deviceNameA;
    std::string deviceNameB;
    uint32_t nominalRateA = 0;
    uint32_t nominalRateB = 0;
    double estimatedRateA = 0.0;
    double estimatedRateB = 0.0;
    double rateRatio = 1.0;               // Ratio of normalized rates (A / B)
    double driftRatePpm = 0.0;            // Rate divergence in ppm: (rateRatio - 1.0) * 1e6
    double relativeDriftPpm = 0.0;        // Alias to driftRatePpm (backwards compatibility)
    double instantaneousOffsetSec = 0.0;  // Current playhead difference: posA(t) - posB(t) (seconds)
    double relativeOffsetSec = 0.0;       // Alias to instantaneousOffsetSec (backwards compatibility)
    double initialOffsetSec = 0.0;        // Initial playhead difference at start of window (seconds)
    double accumulatedDriftSec = 0.0;     // Net change in offset: instantaneousOffset - initialOffset (seconds)
    double measurementDurationSec = 0.0;  // Effective measurement duration (seconds)
    double confidence = 1.0;              // Combined goodness of fit: rSquaredA * rSquaredB (0..1)
    size_t sampleCountA = 0;
    size_t sampleCountB = 0;
    double windowRequestedSec = 0.0;      // 0.0 = full history
    bool isValid = false;
};

class DriftEstimator {
public:
    DriftEstimator() = default;

    // Estimate relative clock drift and offset between two device clocks across full history
    [[nodiscard]] static PairwiseDriftEstimate estimate(
        const DeviceClock& clockA,
        const DeviceClock& clockB,
        const std::string& nameA = "",
        const std::string& nameB = "");

    // Estimate relative clock drift and offset over a specific trailing time window
    [[nodiscard]] static PairwiseDriftEstimate estimateOverWindow(
        const DeviceClock& clockA,
        const DeviceClock& clockB,
        double windowSec,
        const std::string& nameA = "",
        const std::string& nameB = "");

    // Pure mathematical helper: calculate single clock ppm error relative to nominal
    [[nodiscard]] static double calculatePpm(double observedRate, double nominalRate);

    // Pure mathematical helper: calculate relative ppm difference between two clocks
    [[nodiscard]] static double calculateRelativePpm(double rateA, double nominalA, double rateB, double nominalB);

    // Pure mathematical helper: calculate instantaneous offset (secA - secB)
    [[nodiscard]] static double calculateOffset(double posSecA, double posSecB);

    // Pure mathematical helper: calculate accumulated drift from change in offset
    [[nodiscard]] static double calculateAccumulatedDrift(double initialOffsetSec, double finalOffsetSec);

    // Pure mathematical helper: calculate drift rate in ppm from accumulated drift over elapsed time
    [[nodiscard]] static double calculateDriftRateFromDelta(double deltaOffsetSec, double elapsedSec);
};

} // namespace syncwave
