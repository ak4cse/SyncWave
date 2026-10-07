#pragma once

#include "DiagnosticsSnapshot.h"
#include <string>
#include <vector>

namespace syncwave {

// Configuration model for initializing and controlling SyncWave
struct SyncWaveConfig {
    std::vector<std::string> outputDeviceIds; // List of target output endpoint IDs or index strings
    std::string syncMode = "adaptive";       // "none", "software", "adaptive"
    std::vector<double> manualDelaysMs;       // Per-output manual delay in ms
    std::vector<double> calibrationOffsetsMs; // Per-output physical calibration offsets in ms
    std::string captureSourceId;              // Specific capture endpoint ID for loopback (empty = default)
    std::string preferredSource = "Tone";     // "Tone", "Capture", "Media"
    std::string logLevel = "INFO";            // "DEBUG", "INFO", "WARN", "ERROR"
    std::string telemetryCsvPath;             // Optional CSV export path
};

} // namespace syncwave
