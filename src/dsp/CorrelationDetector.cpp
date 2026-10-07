#include "CorrelationDetector.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace syncwave {

CorrelationDetector::CorrelationDetector(const CorrelationConfig& config)
    : config_(config) {}

CorrelationResult CorrelationDetector::detect(
    const std::vector<float>& reference,
    const std::vector<float>& targetSignal,
    uint32_t sampleRate) const
{
    return detect(reference.data(), reference.size(), targetSignal.data(), targetSignal.size(), sampleRate);
}

CorrelationResult CorrelationDetector::detect(
    const float* reference,
    size_t refCount,
    const float* targetSignal,
    size_t targetCount,
    uint32_t sampleRate) const
{
    CorrelationResult res;

    if (!reference || !targetSignal || refCount == 0 || targetCount < refCount || sampleRate == 0) {
        return res;
    }

    const size_t M = refCount;
    const size_t N = targetCount;
    const size_t numLags = N - M + 1;

    // 1. Compute reference statistics (mean and energy about mean)
    double refSum = 0.0;
    for (size_t m = 0; m < M; ++m) {
        refSum += reference[m];
    }
    const double refMean = refSum / static_cast<double>(M);

    double refVar = 0.0;
    for (size_t m = 0; m < M; ++m) {
        double d = reference[m] - refMean;
        refVar += d * d;
    }

    if (refVar <= 1e-12) {
        // Flat reference template has no correlation power
        return res;
    }
    const double refStdDev = std::sqrt(refVar);

    // 2. Sliding window sums for target signal mean and variance
    double targetSum = 0.0;
    double targetSqSum = 0.0;
    for (size_t m = 0; m < M; ++m) {
        double v = targetSignal[m];
        targetSum += v;
        targetSqSum += v * v;
    }

    std::vector<float> corrScores(numLags, 0.0f);
    float maxScore = -1.0f;
    size_t bestLag = 0;

    for (size_t k = 0; k < numLags; ++k) {
        if (k > 0) {
            // Update running sums
            double outgoing = targetSignal[k - 1];
            double incoming = targetSignal[k + M - 1];
            targetSum += (incoming - outgoing);
            targetSqSum += (incoming * incoming - outgoing * outgoing);
        }

        // Variance of target window
        double winVar = targetSqSum - (targetSum * targetSum / static_cast<double>(M));
        if (winVar < 1e-12) {
            corrScores[k] = 0.0f;
            continue;
        }
        double winStdDev = std::sqrt(winVar);

        // Dot product between reference and target window
        const float* yPtr = targetSignal + k;
        double dot = 0.0;
        for (size_t m = 0; m < M; ++m) {
            dot += (reference[m] - refMean) * yPtr[m];
        }

        double score = dot / (refStdDev * winStdDev);
        float clampedScore = static_cast<float>(std::clamp(score, -1.0, 1.0));
        corrScores[k] = clampedScore;

        if (clampedScore > maxScore) {
            maxScore = clampedScore;
            bestLag = k;
        }
    }

    res.peakScore = maxScore;
    res.peakIndex = static_cast<double>(bestLag);

    // 3. Sub-sample parabolic interpolation
    if (bestLag > 0 && bestLag + 1 < numLags) {
        float alpha = corrScores[bestLag - 1];
        float beta = corrScores[bestLag];
        float gamma = corrScores[bestLag + 1];

        float denom = 2.0f * (alpha - 2.0f * beta + gamma);
        if (std::abs(denom) > 1e-7f) {
            float delta = (alpha - gamma) / denom;
            if (std::abs(delta) < 1.0f) {
                res.peakIndex = static_cast<double>(bestLag) + static_cast<double>(delta);
            }
        }
    }

    res.arrivalTimeSec = res.peakIndex / static_cast<double>(sampleRate);

    // 4. Compute noise floor RMS and secondary peak away from the primary correlation peak
    const size_t radius = std::max(static_cast<size_t>(config_.exclusionRadiusFrames), M / 4);
    double noiseSqSum = 0.0;
    size_t noiseCount = 0;
    float secPeakScore = -1.0f;
    size_t secPeakLag = 0;

    for (size_t k = 0; k < numLags; ++k) {
        if (k + radius < bestLag || k > bestLag + radius) {
            float val = corrScores[k];
            noiseSqSum += (val * val);
            ++noiseCount;
            if (val > secPeakScore) {
                secPeakScore = val;
                secPeakLag = k;
            }
        }
    }

    if (noiseCount > 0) {
        res.noiseFloorRms = static_cast<float>(std::sqrt(noiseSqSum / static_cast<double>(noiseCount)));
    } else {
        res.noiseFloorRms = 0.001f;
    }

    // Secondary peak telemetry
    if (secPeakScore > 0.0f) {
        res.secondaryPeakScore = secPeakScore;
        res.secondaryPeakIndex = static_cast<double>(secPeakLag);
        res.secondaryArrivalTimeSec = res.secondaryPeakIndex / static_cast<double>(sampleRate);
        res.peakSeparationMs = (res.secondaryArrivalTimeSec - res.arrivalTimeSec) * 1000.0;
        res.peakToSecondaryRatio = res.peakScore / std::max(secPeakScore, 1e-4f);
    } else {
        res.secondaryPeakScore = 0.0f;
        res.secondaryPeakIndex = 0.0;
        res.secondaryArrivalTimeSec = 0.0;
        res.peakSeparationMs = 0.0;
        res.peakToSecondaryRatio = (res.peakScore > 0.0f) ? 100.0f : 0.0f;
    }

    // 5. Peak-to-Noise Ratio and Confidence
    res.peakToNoiseRatio = res.peakScore / std::max(res.noiseFloorRms, 1e-4f);

    float pnrScore = std::clamp((res.peakToNoiseRatio - 1.0f) / 6.0f, 0.0f, 1.0f);
    float scoreWeight = std::clamp(res.peakScore / 0.30f, 0.3f, 1.0f);
    res.confidence = std::clamp(pnrScore * scoreWeight, 0.0f, 1.0f);

    res.isDetected = (res.peakScore >= config_.minScoreThreshold &&
                      res.peakToNoiseRatio >= config_.minPnrThreshold &&
                      res.confidence >= 0.25f);

    return res;
}

} // namespace syncwave
