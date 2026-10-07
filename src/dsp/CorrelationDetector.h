#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

namespace syncwave {

struct CorrelationConfig {
    float minScoreThreshold = 0.08f;  // Minimum normalized cross-correlation peak score (prompt: acoustic 0.08)
    float minPnrThreshold = 3.0f;     // Minimum peak-to-noise ratio (prompt: 3.0)
    uint32_t exclusionRadiusFrames = 480; // Radius around peak excluded from noise floor calculation (~10 ms @ 48kHz)
};

struct CorrelationResult {
    bool isDetected = false;
    float peakScore = 0.0f;           // Normalized correlation score [-1.0 .. +1.0]
    double peakIndex = 0.0;           // Sub-sample accurate peak arrival index in target signal
    float noiseFloorRms = 0.0f;       // RMS level of correlation values away from the peak
    float peakToNoiseRatio = 0.0f;    // peakScore / noiseFloorRms
    float confidence = 0.0f;          // Estimated detection confidence [0.0 .. 1.0]
    double arrivalTimeSec = 0.0;      // peakIndex / sampleRate

    // Multi-peak & reflection diagnostics (M11.1)
    float secondaryPeakScore = 0.0f;       // Score of the strongest secondary peak outside exclusion radius
    double secondaryPeakIndex = 0.0;       // Sample index of secondary peak
    double secondaryArrivalTimeSec = 0.0;  // Arrival time of secondary peak
    double peakSeparationMs = 0.0;         // (secondaryArrivalTimeSec - arrivalTimeSec) * 1000.0
    float peakToSecondaryRatio = 0.0f;     // peakScore / secondaryPeakScore (inf/100 if none)
};

class CorrelationDetector {
public:
    explicit CorrelationDetector(const CorrelationConfig& config = {});

    // Detects arrival of reference template within targetSignal.
    // Returns CorrelationResult with sub-sample peak location and confidence metrics.
    [[nodiscard]] CorrelationResult detect(
        const float* reference,
        size_t refCount,
        const float* targetSignal,
        size_t targetCount,
        uint32_t sampleRate) const;

    // Overload for std::vector
    [[nodiscard]] CorrelationResult detect(
        const std::vector<float>& reference,
        const std::vector<float>& targetSignal,
        uint32_t sampleRate) const;

    [[nodiscard]] const CorrelationConfig& config() const { return config_; }
    void setConfig(const CorrelationConfig& config) { config_ = config; }

private:
    CorrelationConfig config_;
};

} // namespace syncwave
