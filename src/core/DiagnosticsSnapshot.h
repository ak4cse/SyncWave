#pragma once

#include "../audio/AudioFormat.h"
#include "../sync/OutputLatencyModel.h"
#include <string>
#include <vector>
#include <cstdint>

namespace syncwave {

// Device runtime diagnostics
struct EndpointDiagnostics {
    std::string deviceId;
    std::string friendlyName;
    std::string state;               // "Active", "Disabled", "Unplugged", etc.
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    bool isAvailable = false;

    // Buffer queue telemetry
    size_t queueFrames = 0;
    size_t queueCapacityFrames = 0;
    uint64_t queueUnderruns = 0;
    uint64_t queueOverruns = 0;
    uint64_t wasapiUnderruns = 0;

    // Timing & synchronization telemetry
    double configuredDelayMs = 0.0;
    uint32_t appliedDelayFrames = 0;
    double estimatedSoftwareLatencyMs = 0.0;
    double calibrationOffsetMs = 0.0;
    double effectiveLatencyMs = 0.0;
    SyncState syncState = SyncState::Disabled;

    // Drift controller telemetry
    std::string driftState;          // "Measuring", "Locked", "Correcting", "Uncertain", "Disconnected"
    double phaseErrorMs = 0.0;
    double filteredPhaseErrorMs = 0.0;
    double driftPpm = 0.0;
    double filteredDriftPpm = 0.0;
    double targetRateAdjustmentPpm = 0.0;
    double activeRateAdjustmentPpm = 0.0;
    double driftConfidence = 0.0;
};

// Media playback diagnostics
struct MediaDiagnostics {
    std::string filePathOrUrl;
    std::string playbackState;       // "Stopped", "Opening", "Buffering", "Playing", "Paused", "Ended", "Error"
    double mediaPositionMs = 0.0;
    double durationMs = 0.0;
    double videoPositionMs = 0.0;
    double audioMasterPositionMs = 0.0;
    double audioAcousticPositionMs = 0.0;
    double audioVideoOffsetMs = 0.0; // T_audio - T_video (+: audio leads, -: audio lags)
    float playbackRate = 1.0f;
    bool isSynchronized = false;
    bool isLoaded = false;
    bool isSeeking = false;
};

// System performance diagnostics
struct SystemDiagnostics {
    double processCpuPercent = 0.0;
    double memoryUsageMb = 0.0;
    double uptimeSec = 0.0;
};

// Complete diagnostics snapshot across the entire SyncWave engine
struct FullDiagnosticsSnapshot {
    // Audio engine & master timeline
    bool isEngineRunning = false;
    std::string activeSource;        // "Tone", "LoopbackCapture", "MediaVlc", "None"
    uint32_t masterSampleRate = 48000;
    uint32_t masterChannels = 2;
    uint64_t masterFramesProduced = 0;
    uint64_t masterFramesDistributed = 0;
    size_t masterBusAvailableFrames = 0;
    size_t masterBusCapacityFrames = 0;

    // Endpoints
    std::vector<EndpointDiagnostics> endpoints;

    // Media subsystem
    MediaDiagnostics media;

    // Host system
    SystemDiagnostics system;
};

} // namespace syncwave
