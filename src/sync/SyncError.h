#pragma once

#include <string>
#include <cstdint>
#include <cmath>

namespace syncwave {

// Represents the normalized synchronization state and error for an audio endpoint
// against the authoritative MasterAudioBus timeline.
// Units:
//   Internal timing is maintained in seconds and frames.
//   phaseError: time difference (targetPlayhead - outputPlayhead).
//   driftPpm: relative clock frequency error (divergence rate).
struct SyncError {
    std::string deviceId;
    std::string deviceName;

    // MasterAudioBus logical timeline position
    double masterTimelineSec = 0.0;
    uint64_t masterTimelineFrames = 0;

    // Output endpoint's actual estimated physical playhead (projected onto master timeline)
    double outputPlayheadSec = 0.0;
    uint64_t outputPlayheadFrames = 0;

    // Target synchronized playhead for this endpoint (master timeline minus target baseline latency)
    double targetPlayheadSec = 0.0;
    uint64_t targetPlayheadFrames = 0;

    // Instantaneous phase error: target - output
    // A positive phase error indicates the output is lagging behind target (needs to speed up).
    // A negative phase error indicates the output is leading ahead of target (needs to slow down).
    double phaseErrorSec = 0.0;
    int64_t phaseErrorFrames = 0;
    double phaseErrorMs = 0.0;

    // Low-pass filtered phase error (used by feedback controller to prevent jitter oscillation)
    double filteredPhaseErrorSec = 0.0;
    double filteredPhaseErrorMs = 0.0;

    // Frequency errors (PPM)
    double rawDriftPpm = 0.0;
    double filteredDriftPpm = 0.0;

    double confidence = 0.0;
    double observationDurationSec = 0.0;
    bool isValid = false;

    // Helper: true if output is lagging behind target (needs speedup)
    [[nodiscard]] bool isLagging() const {
        return phaseErrorSec > 0.0;
    }

    // Helper: true if output is leading ahead of target (needs slowdown)
    [[nodiscard]] bool isLeading() const {
        return phaseErrorSec < 0.0;
    }

    // Helper: absolute phase error magnitude in milliseconds
    [[nodiscard]] double absPhaseErrorMs() const {
        return std::abs(phaseErrorMs);
    }

    // Helper: absolute filtered phase error magnitude in milliseconds
    [[nodiscard]] double absFilteredPhaseErrorMs() const {
        return std::abs(filteredPhaseErrorMs);
    }
};

} // namespace syncwave
