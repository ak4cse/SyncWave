#include "AcousticCalibrator.h"
#include "DelayBuffer.h"
#include "../windows/AcousticCapture.h"
#include "../windows/WasapiOutput.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <thread>
#include <cstring>
#include <iostream>

namespace syncwave {

AcousticCalibrator::AcousticCalibrator(const AcousticCalibrationConfig& config)
    : config_(config) {}

void AcousticCalibrator::computeStatistics(
    const std::vector<SingleRunMeasurement>& validMeasurements,
    AcousticCalibrationResult& result,
    const AcousticCalibrationConfig& config)
{
    if (validMeasurements.empty()) {
        result.isValid = false;
        result.validationMessage = "No valid runs detected (all runs failed correlation or bounds)";
        return;
    }

    std::vector<double> latencies;
    latencies.reserve(validMeasurements.size());
    for (const auto& m : validMeasurements) {
        latencies.push_back(m.measuredLatencyMs);
    }

    const size_t K = latencies.size();
    std::sort(latencies.begin(), latencies.end());
    double median = 0.0;
    if (K % 2 == 1) {
        median = latencies[K / 2];
    } else {
        median = 0.5 * (latencies[K / 2 - 1] + latencies[K / 2]);
    }

    // Compute MAD (Median Absolute Deviation)
    std::vector<double> absDevs;
    absDevs.reserve(K);
    for (double val : latencies) {
        absDevs.push_back(std::abs(val - median));
    }
    std::sort(absDevs.begin(), absDevs.end());
    double mad = (K % 2 == 1) ? absDevs[K / 2] : 0.5 * (absDevs[K / 2 - 1] + absDevs[K / 2]);

    // Outlier rejection threshold: max(3.0 * MAD, 15.0 ms)
    double threshold = std::max(3.0 * (mad > 0.0 ? mad : 1.0), 15.0);

    std::vector<const SingleRunMeasurement*> retained;
    double sumLat = 0.0;
    double sumConf = 0.0;
    double minLat = 1e9;
    double maxLat = -1e9;

    for (const auto& m : validMeasurements) {
        if (std::abs(m.measuredLatencyMs - median) <= threshold) {
            retained.push_back(&m);
            sumLat += m.measuredLatencyMs;
            sumConf += m.confidence;
            minLat = std::min(minLat, m.measuredLatencyMs);
            maxLat = std::max(maxLat, m.measuredLatencyMs);
        }
    }

    if (retained.empty()) {
        for (const auto& m : validMeasurements) {
            retained.push_back(&m);
            sumLat += m.measuredLatencyMs;
            sumConf += m.confidence;
            minLat = std::min(minLat, m.measuredLatencyMs);
            maxLat = std::max(maxLat, m.measuredLatencyMs);
        }
    }

    const size_t R = retained.size();
    result.validRuns = static_cast<uint32_t>(R);
    result.medianLatencyMs = median;
    result.meanLatencyMs = sumLat / static_cast<double>(R);
    result.madMs = mad;
    result.averageConfidence = static_cast<float>(sumConf / static_cast<double>(R));
    result.minLatencyMs = minLat;
    result.maxLatencyMs = maxLat;

    if (R > 1) {
        double sqDiffSum = 0.0;
        for (const auto* m : retained) {
            double diff = m->measuredLatencyMs - result.meanLatencyMs;
            sqDiffSum += diff * diff;
        }
        result.stdDevMs = std::sqrt(sqDiffSum / static_cast<double>(R - 1));
    } else {
        result.stdDevMs = 0.0;
    }

    // M11.1 Stricter acceptance criteria:
    // 1. Must satisfy minimum valid runs (e.g. >= 5 out of >= 7)
    // 2. Standard deviation must not exceed max acceptable limit (<= 15.0 ms)
    // 3. Average confidence must be above threshold (>= minConfidence)
    if (result.validRuns < config.minValidRuns) {
        result.isValid = false;
        result.validationMessage = "Insufficient valid runs (" + std::to_string(result.validRuns) +
                                   " < required " + std::to_string(config.minValidRuns) + ")";
    } else if (result.stdDevMs > config.maxAcceptableStdDevMs) {
        result.isValid = false;
        result.validationMessage = "Uncertainty exceeded (" + std::to_string(result.stdDevMs) +
                                   " ms > allowed " + std::to_string(config.maxAcceptableStdDevMs) + " ms)";
    } else if (result.averageConfidence < config.minConfidence) {
        result.isValid = false;
        result.validationMessage = "Confidence too low (" + std::to_string(result.averageConfidence) +
                                   " < required " + std::to_string(config.minConfidence) + ")";
    } else {
        result.isValid = true;
        result.validationMessage = "Calibration passed validation criteria";
    }
}

AcousticCalibrationResult AcousticCalibrator::evaluateSyntheticRuns(
    const std::string& deviceId,
    const std::string& deviceName,
    const std::string& micId,
    const std::string& micName,
    const std::vector<std::vector<float>>& capturedBuffers,
    const std::vector<double>& knownOffsetsSec,
    uint32_t micSampleRate,
    uint32_t outSampleRate) const
{
    AcousticCalibrationResult result;
    result.deviceId = deviceId;
    result.deviceName = deviceName;
    result.microphoneId = micId;
    result.microphoneName = micName;
    result.totalRuns = static_cast<uint32_t>(capturedBuffers.size());

    ChirpGenerator generator(config_.chirpParams);
    const auto refChirp = generator.generateReferenceChirp();
    const double emissionSec = static_cast<double>(generator.chirpMasterFrameIndex()) / static_cast<double>(outSampleRate);

    CorrelationDetector detector(config_.correlationConfig);
    std::vector<SingleRunMeasurement> validRuns;

    for (size_t i = 0; i < capturedBuffers.size(); ++i) {
        SingleRunMeasurement run;
        run.runIndex = static_cast<uint32_t>(i + 1);

        const auto& buf = capturedBuffers[i];
        auto corr = detector.detect(refChirp, buf, micSampleRate);

        run.peakScore = corr.peakScore;
        run.peakToNoiseRatio = corr.peakToNoiseRatio;
        run.confidence = corr.confidence;
        run.noiseFloorRms = corr.noiseFloorRms;
        run.secondaryPeakScore = corr.secondaryPeakScore;
        run.peakSeparationMs = corr.peakSeparationMs;
        run.peakToSecondaryRatio = corr.peakToSecondaryRatio;

        if (corr.isDetected) {
            run.isSuccess = true;
            double arrivalSec = corr.arrivalTimeSec;
            double extraOffset = (i < knownOffsetsSec.size()) ? knownOffsetsSec[i] : 0.0;
            run.measuredLatencyMs = (extraOffset + arrivalSec - emissionSec) * 1000.0;
            run.secondaryLatencyMs = (extraOffset + corr.secondaryArrivalTimeSec - emissionSec) * 1000.0;
            validRuns.push_back(run);
        } else {
            run.isSuccess = false;
            if (corr.peakScore < config_.correlationConfig.minScoreThreshold) {
                run.failureReason = "Peak score below threshold (" + std::to_string(corr.peakScore) + ")";
            } else if (corr.peakToNoiseRatio < config_.correlationConfig.minPnrThreshold) {
                run.failureReason = "Peak-to-noise ratio below threshold (" + std::to_string(corr.peakToNoiseRatio) + ")";
            } else {
                run.failureReason = "Confidence below threshold (" + std::to_string(corr.confidence) + ")";
            }
        }

        result.runs.push_back(run);
    }

    computeStatistics(validRuns, result, config_);
    return result;
}

AcousticCalibrationResult AcousticCalibrator::calibrate(
    const AudioDevice& outputDevice,
    const AudioDevice& microphoneDevice,
    CalibrationProgressCallback progressCb) const
{
    AcousticCalibrationResult result;
    result.deviceId = outputDevice.id;
    result.deviceName = outputDevice.name;
    result.microphoneId = microphoneDevice.id;
    result.microphoneName = microphoneDevice.name;
    result.totalRuns = config_.runs;

    // 1. Initialize output device
    auto wasapiOut = std::make_unique<WasapiOutput>();
    if (!wasapiOut->open(outputDevice.id)) {
        std::cerr << "AcousticCalibrator: Failed to open output device: " << outputDevice.name << "\n";
        return result;
    }
    if (!wasapiOut->initialize()) {
        std::cerr << "AcousticCalibrator: Failed to initialize output device: " << outputDevice.name << "\n";
        wasapiOut->close();
        return result;
    }

    const auto outFmt = wasapiOut->format();
    const uint32_t outRate = outFmt.sampleRate > 0 ? outFmt.sampleRate : 48000;

    // 2. Initialize microphone capture device
    auto capture = std::make_unique<AcousticCapture>();
    if (!capture->open(microphoneDevice.id)) {
        std::cerr << "AcousticCalibrator: Failed to open microphone: " << microphoneDevice.name << "\n";
        wasapiOut->close();
        return result;
    }
    if (!capture->initialize()) {
        std::cerr << "AcousticCalibrator: Failed to initialize microphone: " << microphoneDevice.name << "\n";
        capture->close();
        wasapiOut->close();
        return result;
    }

    const uint32_t micRate = capture->sampleRate() > 0 ? capture->sampleRate() : 48000;

    // 3. Configure ChirpGenerator for the output and microphone sample rates
    auto outChirpParams = config_.chirpParams;
    outChirpParams.sampleRate = outRate;
    ChirpGenerator generator(outChirpParams);
    const size_t totalSeqFrames = static_cast<size_t>(generator.totalFrames());

    auto micChirpParams = config_.chirpParams;
    micChirpParams.sampleRate = micRate;
    ChirpGenerator micGenerator(micChirpParams);
    const auto refChirp = micGenerator.generateReferenceChirp();

    // Microphone capture needs to run slightly longer than the chirp sequence to capture room decay
    const size_t extraMicFrames = micRate / 2; // +500 ms margin
    const size_t totalMicFrames = static_cast<size_t>(
        (static_cast<double>(totalSeqFrames) / outRate) * micRate) + extraMicFrames;

    CorrelationDetector detector(config_.correlationConfig);
    std::vector<SingleRunMeasurement> validRuns;

    // Pre-generate full sequence audio in output format
    std::vector<float> chirpAudio(totalSeqFrames * outFmt.channels);
    generator.reset();
    generator.generateFrames(chirpAudio.data(), totalSeqFrames, outFmt.channels);

    DelayBuffer delayBuffer(static_cast<size_t>(outRate * 3));
    if (config_.appliedDelayMs > 0.0) {
        delayBuffer.setDelayMs(config_.appliedDelayMs, outRate);
    }
    std::vector<float> processScratch;

    std::atomic<bool> chirpInProgress{false};
    std::atomic<size_t> chirpFramePos{0};
    std::atomic<bool> emissionCaptured{false};
    std::chrono::steady_clock::time_point emissionTimePoint;

    bool outStarted = wasapiOut->start([&](uint8_t* destBytes, uint32_t framesRequested, const AudioFormat& fmt) {
        float* dest = reinterpret_cast<float*>(destBytes);
        const size_t ch = fmt.channels > 0 ? fmt.channels : 2;

        if (processScratch.size() < framesRequested * ch) {
            processScratch.resize(framesRequested * ch, 0.0f);
        }
        float* scratch = processScratch.data();

        if (!chirpInProgress.load(std::memory_order_acquire)) {
            std::memset(scratch, 0, framesRequested * ch * sizeof(float));
            if (config_.appliedDelayMs > 0.0 && ch == 2) {
                delayBuffer.process(scratch, dest, framesRequested);
            } else {
                std::memset(dest, 0, framesRequested * ch * sizeof(float));
            }
            return;
        }

        size_t current = chirpFramePos.load(std::memory_order_relaxed);
        if (current == 0 && !emissionCaptured.load(std::memory_order_relaxed)) {
            emissionTimePoint = std::chrono::steady_clock::now();
            emissionCaptured.store(true, std::memory_order_relaxed);
        }

        size_t remaining = totalSeqFrames > current ? (totalSeqFrames - current) : 0;
        size_t framesToCopy = std::min(static_cast<size_t>(framesRequested), remaining);

        if (framesToCopy > 0) {
            const float* src = chirpAudio.data() + (current * ch);
            std::memcpy(scratch, src, framesToCopy * ch * sizeof(float));
        }
        if (framesToCopy < framesRequested) {
            std::memset(scratch + framesToCopy * ch, 0, (framesRequested - framesToCopy) * ch * sizeof(float));
        }

        chirpFramePos.fetch_add(framesToCopy, std::memory_order_relaxed);
        if (current + framesToCopy >= totalSeqFrames) {
            chirpInProgress.store(false, std::memory_order_release);
        }

        if (config_.appliedDelayMs > 0.0 && ch == 2) {
            delayBuffer.process(scratch, dest, framesRequested);
        } else {
            std::memcpy(dest, scratch, framesRequested * ch * sizeof(float));
        }
    });

    if (!outStarted) {
        std::cerr << "AcousticCalibrator: Failed to start WasapiOutput on " << outputDevice.name << "\n";
        wasapiOut->close();
        return result;
    }

    // Allow output stream to reach steady-state
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    for (uint32_t runIdx = 1; runIdx <= config_.runs; ++runIdx) {
        SingleRunMeasurement measurement;
        measurement.runIndex = runIdx;

        chirpFramePos.store(0, std::memory_order_relaxed);
        emissionCaptured.store(false, std::memory_order_relaxed);

        if (!capture->startRecording(totalMicFrames)) {
            std::cerr << "AcousticCalibrator: Failed to start recording on run " << runIdx << "\n";
            measurement.isSuccess = false;
            result.runs.push_back(measurement);
            if (progressCb) progressCb(runIdx, config_.runs, measurement);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        // Brief delay (50 ms) to ensure capture thread is actively reading packets
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Trigger chirp playback in the already running audio stream
        chirpInProgress.store(true, std::memory_order_release);

        // Wait until microphone has finished capturing the target frame count
        while (capture->isRecording() && !capture->isComplete()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        capture->stopRecording();
        chirpInProgress.store(false, std::memory_order_release);

        auto micStartTime = capture->startTime();
        auto recorded = capture->getRecordedSamples();
        auto corr = detector.detect(refChirp, recorded, micRate);

        float maxAmp = 0.0f;
        for (float s : recorded) {
            maxAmp = std::max(maxAmp, std::abs(s));
        }
        measurement.maxMicAmplitude = maxAmp;
        measurement.peakScore = corr.peakScore;
        measurement.peakToNoiseRatio = corr.peakToNoiseRatio;
        measurement.confidence = corr.confidence;
        measurement.noiseFloorRms = corr.noiseFloorRms;
        measurement.secondaryPeakScore = corr.secondaryPeakScore;
        measurement.peakSeparationMs = corr.peakSeparationMs;
        measurement.peakToSecondaryRatio = corr.peakToSecondaryRatio;

        if (corr.isDetected && emissionCaptured.load(std::memory_order_relaxed)) {
            // Emission time: emissionTimePoint + lead-in silence
            double leadInSec = static_cast<double>(generator.chirpMasterFrameIndex()) / static_cast<double>(outRate);
            auto emissionTime = emissionTimePoint + std::chrono::microseconds(static_cast<int64_t>(leadInSec * 1000000.0));

            // Arrival time: micStartTime + detected arrival index
            double arrivalOffsetSec = corr.arrivalTimeSec;
            auto arrivalTime = micStartTime + std::chrono::microseconds(static_cast<int64_t>(arrivalOffsetSec * 1000000.0));

            double latencySec = std::chrono::duration<double>(arrivalTime - emissionTime).count();
            double latencyMs = latencySec * 1000.0;
            measurement.measuredLatencyMs = latencyMs;
            measurement.secondaryLatencyMs = latencyMs + corr.peakSeparationMs;

            // Plausibility check: arrival latency must be positive and <= 800 ms
            if (latencyMs >= 0.0 && latencyMs <= 800.0) {
                measurement.isSuccess = true;
                validRuns.push_back(measurement);
            } else {
                measurement.isSuccess = false;
                measurement.failureReason = "Measured arrival (" + std::to_string(latencyMs) + " ms) outside plausible range [0..800 ms]";
            }
        } else {
            measurement.isSuccess = false;
            if (!corr.isDetected) {
                if (corr.peakScore < config_.correlationConfig.minScoreThreshold) {
                    measurement.failureReason = "Peak score below threshold (" + std::to_string(corr.peakScore) + ")";
                } else if (corr.peakToNoiseRatio < config_.correlationConfig.minPnrThreshold) {
                    measurement.failureReason = "Peak-to-noise ratio below threshold (" + std::to_string(corr.peakToNoiseRatio) + ")";
                } else {
                    measurement.failureReason = "Confidence below threshold (" + std::to_string(corr.confidence) + ")";
                }
            } else {
                measurement.failureReason = "Hardware emission timestamp not captured";
            }
        }

        result.runs.push_back(measurement);
        if (progressCb) {
            progressCb(runIdx, config_.runs, measurement);
        }

        // Brief settling delay between runs (200 ms)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    wasapiOut->stop();
    wasapiOut->close();
    capture->close();

    computeStatistics(validRuns, result, config_);
    return result;
}

} // namespace syncwave
