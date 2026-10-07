#pragma once

#include "../dsp/ChirpGenerator.h"
#include "../dsp/CorrelationDetector.h"
#include "CalibrationStore.h"
#include "../windows/DeviceManager.h"
#include <vector>
#include <string>
#include <functional>

namespace syncwave {

struct SingleRunMeasurement {
    uint32_t runIndex = 0;
    bool isSuccess = false;
    double measuredLatencyMs = 0.0;
    float peakScore = 0.0f;
    float peakToNoiseRatio = 0.0f;
    float confidence = 0.0f;
    float noiseFloorRms = 0.0f;
    float maxMicAmplitude = 0.0f;

    // Multi-peak & reflection diagnostics (M11.1)
    float secondaryPeakScore = 0.0f;
    double secondaryLatencyMs = 0.0;
    double peakSeparationMs = 0.0;
    float peakToSecondaryRatio = 0.0f;
    std::string failureReason;
};

struct AcousticCalibrationResult {
    bool isValid = false;
    std::string deviceId;
    std::string deviceName;
    std::string microphoneId;
    std::string microphoneName;
    double medianLatencyMs = 0.0;
    double meanLatencyMs = 0.0;
    double stdDevMs = 0.0;        // Uncertainty (+/- ms)
    double madMs = 0.0;           // Median Absolute Deviation (+/- ms)
    double minLatencyMs = 0.0;
    double maxLatencyMs = 0.0;
    float averageConfidence = 0.0f;
    uint32_t totalRuns = 0;
    uint32_t validRuns = 0;
    std::string validationMessage;
    std::vector<SingleRunMeasurement> runs;

    [[nodiscard]] AcousticCalibrationRecord toRecord() const {
        AcousticCalibrationRecord r;
        r.deviceId = deviceId;
        r.deviceName = deviceName;
        r.microphoneId = microphoneId;
        r.microphoneName = microphoneName;
        r.measurementType = "end_to_end_acoustic_arrival";
        r.measuredLatencyMs = medianLatencyMs;
        r.uncertaintyMs = stdDevMs;
        r.madMs = madMs;
        r.confidence = averageConfidence;
        r.runCount = totalRuns;
        r.validRunCount = validRuns;
        return r;
    }
};

struct AcousticCalibrationConfig {
    uint32_t runs = 7;                   // M11.1: Default 7 runs
    uint32_t minValidRuns = 5;          // M11.1: Require at least 5 valid runs
    ChirpParameters chirpParams;
    CorrelationConfig correlationConfig;
    double maxAcceptableStdDevMs = 15.0;
    float minConfidence = 0.40f;
    double appliedDelayMs = 0.0; // Intentional post-compensation delay in ms (DelayBuffer)
};

using CalibrationProgressCallback = std::function<void(uint32_t currentRun, uint32_t totalRuns, const SingleRunMeasurement& lastRun)>;

class AcousticCalibrator {
public:
    explicit AcousticCalibrator(const AcousticCalibrationConfig& config = {});

    // Run live acoustic calibration using physical output device and microphone
    [[nodiscard]] AcousticCalibrationResult calibrate(
        const AudioDevice& outputDevice,
        const AudioDevice& microphoneDevice,
        CalibrationProgressCallback progressCb = nullptr) const;

    // Synthetic calibration evaluation for offline analysis or automated testing
    [[nodiscard]] AcousticCalibrationResult evaluateSyntheticRuns(
        const std::string& deviceId,
        const std::string& deviceName,
        const std::string& micId,
        const std::string& micName,
        const std::vector<std::vector<float>>& capturedBuffers,
        const std::vector<double>& knownOffsetsSec,
        uint32_t micSampleRate,
        uint32_t outSampleRate) const;

    [[nodiscard]] const AcousticCalibrationConfig& config() const { return config_; }
    void setConfig(const AcousticCalibrationConfig& config) { config_ = config; }

private:
    AcousticCalibrationConfig config_;

    static void computeStatistics(
        const std::vector<SingleRunMeasurement>& validMeasurements,
        AcousticCalibrationResult& result,
        const AcousticCalibrationConfig& config);
};

} // namespace syncwave
