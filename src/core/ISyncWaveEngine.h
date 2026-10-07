#pragma once

#include "DiagnosticsSnapshot.h"
#include "SyncWaveConfig.h"
#include "../windows/DeviceManager.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace syncwave {

// Public Engine API boundary: abstracts all internals (WASAPI, COM, Resamplers, LibVLC)
// away from presentation layers (CLI, future Qt/WinUI GUI).
class ISyncWaveEngine {
public:
    virtual ~ISyncWaveEngine() = default;

    // --- Device Enumeration & Management ---
    virtual std::vector<AudioDevice> enumerateOutputDevices(bool activeOnly = true) = 0;
    virtual std::vector<AudioDevice> enumerateCaptureDevices(bool activeOnly = true) = 0;
    virtual std::optional<AudioDevice> getDefaultOutputDevice() = 0;
    virtual std::optional<AudioDevice> getDefaultCaptureDevice() = 0;

    // --- Playback Control ---
    virtual bool startTone(const std::vector<std::string>& outputIds, double frequencyHz = 440.0, double volume = 0.25) = 0;
    virtual bool startCapture(const std::string& captureSourceId, const std::vector<std::string>& outputIds) = 0;
    virtual bool startMedia(const std::string& filePathOrUrl, const std::vector<std::string>& outputIds) = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual bool seekMedia(double targetTimeMs) = 0;

    // --- Synchronization & Delay Configuration ---
    virtual void setSyncMode(const std::string& mode) = 0; // "none", "software", "adaptive"
    virtual void setManualDelays(const std::vector<double>& delaysMs) = 0;
    virtual void setCalibrationOffsets(const std::vector<double>& offsetsMs) = 0;

    // --- Diagnostics & State Query ---
    virtual bool isRunning() const = 0;
    virtual FullDiagnosticsSnapshot getDiagnostics() = 0;
};

// Factory function to create engine instance
std::unique_ptr<ISyncWaveEngine> createSyncWaveEngine();

} // namespace syncwave
