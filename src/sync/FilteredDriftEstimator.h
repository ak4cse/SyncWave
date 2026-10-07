#pragma once

#include "DeviceClock.h"
#include "SyncError.h"
#include <string>
#include <chrono>

namespace syncwave {

struct FilteredDriftResult {
    double rawDriftPpm = 0.0;
    double filteredDriftPpm = 0.0;
    double rawPhaseErrorSec = 0.0;
    double filteredPhaseErrorSec = 0.0;
    double rawPhaseErrorMs = 0.0;
    double filteredPhaseErrorMs = 0.0;
    double confidence = 0.0;             // Goodness-of-fit and observation score in [0.0, 1.0]
    double observationDurationSec = 0.0;
    size_t sampleCount = 0;
    bool isValid = false;
    bool isOutlierRejected = false;
};

class FilteredDriftEstimator {
public:
    explicit FilteredDriftEstimator(
        double minObservationSec = 5.0,
        size_t minSamples = 30,
        double maxAllowedPpm = 500.0,
        double emaAlphaDrift = 0.15,
        double emaAlphaPhase = 0.20);
    ~FilteredDriftEstimator() = default;

    // Reset filter state
    void reset();

    // Ingest a new measurement cycle
    // Consumes device clock rate estimation, target playhead, and output playhead.
    // Returns filtered drift and phase error metrics.
    FilteredDriftResult update(
        const DeviceClock& clock,
        double targetPlayheadSec,
        double outputPlayheadSec,
        double windowSec = 10.0);

    // Convenience overload producing a full SyncError record
    SyncError updateSyncError(
        const std::string& deviceId,
        const std::string& deviceName,
        const DeviceClock& clock,
        double masterTimelineSec,
        uint64_t masterTimelineFrames,
        double targetPlayheadSec,
        double outputPlayheadSec,
        uint32_t masterSampleRate = 48000,
        double windowSec = 10.0);

    [[nodiscard]] double minObservationDurationSec() const { return minObservationSec_; }
    void setMinObservationDurationSec(double sec) { minObservationSec_ = sec; }

    [[nodiscard]] size_t minSamples() const { return minSamples_; }
    void setMinSamples(size_t n) { minSamples_ = n; }

    [[nodiscard]] double maxAllowedPpm() const { return maxAllowedPpm_; }
    void setMaxAllowedPpm(double ppm) { maxAllowedPpm_ = ppm; }

    [[nodiscard]] bool hasBaseline() const { return hasBaseline_; }
    [[nodiscard]] double baselinePhaseOffsetSec() const { return baselinePhaseOffsetSec_; }
    void setBaselinePhaseOffsetSec(double offsetSec) {
        baselinePhaseOffsetSec_ = offsetSec;
        hasBaseline_ = true;
    }
    [[nodiscard]] double latestFilteredDriftPpm() const { return filteredDriftPpm_; }
    [[nodiscard]] double latestFilteredPhaseErrorSec() const { return filteredPhaseErrorSec_; }

private:
    double minObservationSec_ = 5.0;
    size_t minSamples_ = 30;
    double maxAllowedPpm_ = 500.0;
    double minConfidence_ = 0.90; // minimum r^2 required
    double emaAlphaDrift_ = 0.15;
    double emaAlphaPhase_ = 0.20;

    bool hasBaseline_ = false;
    double baselinePhaseOffsetSec_ = 0.0;
    double filteredDriftPpm_ = 0.0;
    double filteredPhaseErrorSec_ = 0.0;
    double lastValidDriftPpm_ = 0.0;
    size_t validUpdateCount_ = 0;
};

} // namespace syncwave
