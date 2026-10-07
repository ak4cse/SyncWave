# SyncWave GUI Architecture & Integration Specification

This document defines the interface boundary between the SyncWave core real-time audio synchronization engine and future presentation layers (such as a Qt, WinUI 3, or Slint desktop GUI).

---

## 1. Architectural Principles

```
┌──────────────────────────────────────────────────┐
│              Presentation Layer                  │
│            (Thin Desktop / Qt GUI)               │
└─────────────────────────┬────────────────────────┘
                          │ Commands & Polling (10–30 Hz)
                          ▼
┌──────────────────────────────────────────────────┐
│             SyncWave Public Engine API           │
│                [ISyncWaveEngine]                 │
└─────────────────────────┬────────────────────────┘
                          │ Owns & Orchestrates
                          ▼
┌──────────────────────────────────────────────────┐
│                 Engine Internals                 │
│                                                  │
│   DeviceManager   ───►   OutputRouter            │
│   AudioEngine     ───►   MasterAudioBus          │
│   SyncController  ───►   DelayBuffer / Resampler │
│   VlcMediaEngine  ───►   WASAPI Render Threads   │
└──────────────────────────────────────────────────┘
```

1. **Strict Ownership Boundary**:
   - The GUI **must never** directly interact with WASAPI COM interfaces, real-time render callbacks, memory buffers, sinc interpolation filters, or LibVLC memory hooks.
   - The GUI interacts solely through the high-level `ISyncWaveEngine` abstract interface.
2. **Lock-Free Presentation Plane**:
   - Audio processing threads run in high-priority Windows MMCSS (`Pro Audio`) mode.
   - The GUI polls diagnostics snapshots asynchronously (typically at 10–30 Hz) via `ISyncWaveEngine::getDiagnostics()`, avoiding thread blocking or lock contention.
3. **Graceful Error Handling**:
   - All public engine calls fail safely with boolean return codes or fallback states without throwing unhandled exceptions or crashing the GUI process.

---

## 2. Public Engine Interface (`ISyncWaveEngine`)

Located in [`src/core/ISyncWaveEngine.h`](file:///c:/Dev/Projects/SyncWave/src/core/ISyncWaveEngine.h).

```cpp
namespace syncwave {

class ISyncWaveEngine {
public:
    virtual ~ISyncWaveEngine() = default;

    // Device Enumeration
    virtual std::vector<AudioDevice> enumerateOutputDevices(bool activeOnly = true) = 0;
    virtual std::vector<AudioDevice> enumerateCaptureDevices(bool activeOnly = true) = 0;
    virtual std::optional<AudioDevice> getDefaultOutputDevice() = 0;
    virtual std::optional<AudioDevice> getDefaultCaptureDevice() = 0;

    // Playback Controls
    virtual bool startTone(const std::vector<std::string>& outputIds, double frequencyHz = 440.0, double volume = 0.25) = 0;
    virtual bool startCapture(const std::string& captureSourceId, const std::vector<std::string>& outputIds) = 0;
    virtual bool startMedia(const std::string& filePathOrUrl, const std::vector<std::string>& outputIds) = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual bool seekMedia(double targetTimeMs) = 0;

    // Synchronization Mode & Manual Overrides
    virtual void setSyncMode(const std::string& mode) = 0; // "none", "software", "adaptive"
    virtual void setManualDelays(const std::vector<double>& delaysMs) = 0;
    virtual void setCalibrationOffsets(const std::vector<double>& offsetsMs) = 0;

    // Status & Diagnostics
    virtual bool isRunning() const = 0;
    virtual FullDiagnosticsSnapshot getDiagnostics() = 0;
};

// Factory instantiation
std::unique_ptr<ISyncWaveEngine> createSyncWaveEngine();

} // namespace syncwave
```

---

## 3. Minimal GUI Presentation Surfaces

A future desktop GUI requires only 6 focused panels:

### 3.1 Device & Output Selector Panel
- Lists all active Windows audio outputs with friendly names and status badges.
- Checkboxes to include endpoints into the synchronized multi-output stream.
- Indicator for Windows default render device.

### 3.2 Synchronization & Timing Control Panel
- **Sync Mode Selector**:
  - `None`: Direct low-latency dispatch without alignment.
  - `Software`: Automatic static alignment based on driver and queue latency.
  - `Adaptive`: Automatic initial delay alignment plus active micro-resampling clock drift tracking.
- **Manual Delay Sliders**: Adjust per-device manual delay offsets in milliseconds ($\pm 200\text{ ms}$).

### 3.3 Media Player Controls Panel
- Open File / URL dialog.
- Play, Pause, Stop buttons.
- Seeking scrub bar bound to `seekMedia(ms)`.
- Volume level slider.

### 3.4 Live Telemetry & Synchronization Status Panel
- **Status indicators per active output**:
  - Phase Error ($\text{ms}$)
  - Clock Drift ($\text{ppm}$)
  - Active Rate Correction ($\text{ppm}$)
  - Queue depth and underrun badge
  - Lock State: `Measuring` (Yellow), `Locked` (Green), `Correcting` (Blue), `Uncertain` (Orange)
- **Media Telemetry**:
  - Current Media Time vs Audio Master Time
  - Instantaneous A/V Offset ($\Delta T_{\text{av}} = T_{\text{audio}} - T_{\text{video}}$)

### 3.5 Host Diagnostics Panel
- Process CPU utilization (%)
- Process Working Set memory footprint ($\text{MB}$)
- Engine Uptime counter

---

## 4. Polling & Data Binding Pattern (Qt Example)

```cpp
// In Qt MainWindow or Controller:
QTimer* diagTimer = new QTimer(this);
connect(diagTimer, &QTimer::timeout, this, [this]() {
    auto diag = engine_->getDiagnostics();
    
    // Update master timeline info
    ui->labelMasterFrames->setText(QString::number(diag.masterFramesProduced));
    ui->labelCpu->setText(QString("%1%").arg(diag.system.processCpuPercent, 0, 'f', 1));
    ui->labelMemory->setText(QString("%1 MB").arg(diag.system.memoryUsageMb, 0, 'f', 1));
    
    // Update endpoints table
    for (const auto& ep : diag.endpoints) {
        // Render phase error and lock badges...
    }
});
diagTimer->start(100); // 10 Hz refresh
```
