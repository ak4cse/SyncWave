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
    double relativeDriftPpm = 0.0;        // (rateRatio - 1.0) * 1e6
    double relativeOffsetSec = 0.0;       // Position time difference: posA - posB (seconds)
    double measurementDurationSec = 0.0;  // Effective measurement duration (seconds)
    size_t sampleCountA = 0;
    size_t sampleCountB = 0;
    bool isValid = false;
};

class DriftEstimator {
public:
    DriftEstimator() = default;

    // Estimate relative clock drift and offset between two device clocks
    [[nodiscard]] static PairwiseDriftEstimate estimate(
        const DeviceClock& clockA,
        const DeviceClock& clockB,
        const std::string& nameA = "",
        const std::string& nameB = "");

    // Pure mathematical helper: calculate single clock ppm error relative to nominal
    [[nodiscard]] static double calculatePpm(double observedRate, double nominalRate);

    // Pure mathematical helper: calculate relative ppm difference between two clocks
    [[nodiscard]] static double calculateRelativePpm(double rateA, double nominalA, double rateB, double nominalB);
};

} // namespace syncwave
