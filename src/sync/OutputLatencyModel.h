#pragma once

#include <string>
#include <cstdint>
#include <cmath>

namespace syncwave {

enum class SyncState {
    Disabled,             // No delay alignment active (all delays = 0)
    Manual,               // Explicit user-configured static delays applied
    SoftwareCalibrated,   // Automatically aligned based on measured software-domain latency
    PhysicallyCalibrated, // User-provided physical acoustic offset applied
    Uncertain             // Measurement failed or was too noisy to determine safe alignment
};

[[nodiscard]] inline std::string syncStateToString(SyncState state) {
    switch (state) {
        case SyncState::Disabled:             return "Disabled";
        case SyncState::Manual:               return "Manual";
        case SyncState::SoftwareCalibrated:   return "SoftwareCalibrated";
        case SyncState::PhysicallyCalibrated: return "PhysicallyCalibrated";
        case SyncState::Uncertain:            return "Uncertain";
        default:                              return "Unknown";
    }
}

struct OutputLatencyModel {
    std::string deviceId;
    std::string deviceName;
    uint32_t sampleRate = 48000;

    // Observable software-domain metrics (in milliseconds)
    double wasapiStreamLatencyMs = 0.0;       // Driver stream latency (IAudioClient::GetStreamLatency)
    double wasapiPaddingMs = 0.0;             // Queued frames in mixer buffer (IAudioClient::GetCurrentPadding)
    double queueLatencyMs = 0.0;              // Buffered frames in SPSC router queue (RingBuffer::availableToRead)
    double resamplerLatencyMs = 0.0;          // Linear interpolation group delay (~0.5 frames at output rate)
    double configuredDelayMs = 0.0;           // Active delay applied by DelayBuffer
    size_t appliedDelayFrames = 0;            // Actual delay line length in device frames

    // Total observable software-path transit time
    double estimatedSoftwareLatencyMs = 0.0;  // wasapiStreamLatencyMs + wasapiPaddingMs + queueLatencyMs + resamplerLatencyMs

    // Optional user-supplied physical acoustic offset (e.g. from acoustic chirp/mic measurement)
    double optionalCalibrationOffsetMs = 0.0;

    // Effective latency used by the synchronization controller for alignment:
    // effectiveLatencyMs = estimatedSoftwareLatencyMs + optionalCalibrationOffsetMs
    double effectiveLatencyMs = 0.0;

    // Current synchronization state
    SyncState syncState = SyncState::Disabled;

    // Helper: recalculates totals from current components
    void updateTotals() {
        estimatedSoftwareLatencyMs = wasapiStreamLatencyMs + wasapiPaddingMs + queueLatencyMs + resamplerLatencyMs;
        effectiveLatencyMs = estimatedSoftwareLatencyMs + optionalCalibrationOffsetMs;
    }
    void recalculate() { updateTotals(); }
};

} // namespace syncwave
