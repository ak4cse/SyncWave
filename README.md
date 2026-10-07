# SyncWave

**SyncWave** is a Windows real-time heterogeneous multi-device audio synchronization engine and media playback pipeline written in modern C++ (C++20).

SyncWave captures system audio (via WASAPI loopback) or streams containerized media (via LibVLC) and renders it concurrently across multiple independent physical audio endpoints (integrated speakers, Bluetooth earbuds, USB headsets, wired headphones, virtual audio sinks) while measuring and compensating for buffering differences, queue latency, and hardware clock drift.

---

## Architecture Principles

1. **CLI & Engine First**: The real-time synchronization engine is completely decoupled from any presentation layer. All timing models, resamplers, and controllers are verified via automated mathematical tests and reproducible hardware benchmarks before adding a GUI.
2. **Authoritative Master Timeline**: SyncWave maintains a single, continuous, monotonic Master Audio Timeline ($T_{\text{master}}$ @ 48 kHz Float32 stereo). Neither media sources (LibVLC) nor physical endpoints (Bluetooth DACs) dictate playback timing.
3. **Strict Separation of Planes**:
   - **Audio Data Plane**: Real-time lock-free PCM movement (`Capture / VLC -> MasterAudioBus -> OutputRouter -> DelayBuffer -> Sinc Resampler -> WASAPI`). No allocations, no mutexes, and no console I/O in render callbacks.
   - **Timing Plane**: Logical master clock, endpoint clock linear regression (`DeviceClock`), out-of-band drift filtering (`FilteredDriftEstimator`), and dual feedforward/feedback closed-loop correction (`DriftController`).
   - **Control Plane**: Endpoint device management, hot-plug notifications (`IMMNotificationClient`), and user commands.
4. **Empirical Measurement over Assumption**: Delays and clock rates are measured directly from hardware clock queries and correlation detection.

---

## Project Status & Milestone Progression

- [x] **Milestones 1–6: WASAPI Multi-Endpoint Engine Foundation**
  - Core Audio endpoint enumeration and hot-plug detection (`IMMNotificationClient`).
  - High-priority MMCSS (`Pro Audio`) event-driven WASAPI shared renderers (`WasapiOutput`).
  - Lock-free Single-Producer / Single-Consumer (SPSC) circular buffers (`RingBuffer`, `MasterAudioBus`).
  - WASAPI loopback capture (`WasapiCapture`) and fan-out distribution router (`OutputRouter`).
  - Concurrent multi-endpoint rendering across heterogeneous sample rates (e.g. 48.0 kHz Realtek + 44.1 kHz Bluetooth realme Buds T310).
- [x] **Milestones 7–8: Hardware Clock Modeling & Playhead Telemetry**
  - Hardware clock regression modeling (`DeviceClock`) using Ordinary Least Squares (OLS) linear regression.
  - Multi-window convergence analysis (1s, 5s, 10s, 30s, 60s) isolating Bluetooth startup ramp artifacts.
  - Pairwise relative drift estimation and software playhead tracking.
- [x] **Milestone 9: Static Delay Alignment & DelayStage**
  - Lock-free post-resampling delay buffers (`DelayBuffer`) aligning endpoints to the slowest path ($T_{\text{target}} = \max_i(L_i)$).
  - Explicit synchronization state tracking (`Disabled`, `SoftwareCalibrated`, `PhysicallyCalibrated`, `Uncertain`).
- [x] **Milestone 10: Controlled Closed-Loop Clock Drift Correction**
  - Sinc micro-resampling with per-frame slew rate limiting ($\le 5.0\text{ ppm/s}$).
  - Filtered drift estimation (`FilteredDriftEstimator`) with outlier rejection and dual EMA smoothing ($\alpha_{\text{drift}} = 0.15$, $\alpha_{\text{phase}} = 0.20$).
  - Closed-loop feedback controller (`DriftController`) with feedforward cancellation ($u_{\text{FF}} = -D$), proportional phase compensation ($K_p = 10.0\text{ ppm/ms}$), and $\pm 100\text{ ppm}$ clamp.
- [x] **Milestone 11: Acoustic Calibration Infrastructure & Verification**
  - Logarithmic sine chirp generation (`ChirpGenerator`) and Tukey-windowed matched filtering (`CorrelationDetector`).
  - Model B End-to-End Acoustic Arrival accounting ($L_{\text{arrival}}$ eliminates double-counting of software driver latency).
  - Physical delay shift experimentally verified on Realtek speakers ($+24.15\text{ ms}$ observed for $+24.28\text{ ms}$ commanded, $0.13\text{ ms}$ error).
- [x] **Milestone 12: Long-Duration Synchronization Stress Validation**
  - 180-second adaptive correction stress tests with zero WASAPI underruns, zero queue overflows, and bounded memory footprints.
  - Disconnect and reconnect fault recovery allowing endpoints to gracefully rejoin the master timeline without engine reinitialization.
- [x] **Milestone 13: Audio/Video Synchronization Architecture & LibVLC Integration**
  - Formal 7-clock timeline hierarchy establishing deterministic mapping between container $\text{PTS}_{\text{media}}$ and $T_{\text{master}}$ frames.
  - LibVLC 3.0.x integration via memory audio callbacks (`amem` Float32 48 kHz stereo) pushing into `MasterAudioBus`.
  - Seek, pause, resume, and flush safety preserving cumulative frame counters across discontinuities.
  - Live 3-output concurrent media streaming with microsecond-resolution A/V telemetry logging.
- [x] **Milestone 14: Production Hardening, Release Baseline & GUI Readiness**
  - Clean public engine interface (`ISyncWaveEngine`) and complete telemetry snapshot model (`DiagnosticsSnapshot`).
  - Comprehensive operational release runbook (`docs/release.md`) and GUI readiness contract (`docs/gui-api.md`).
  - 1,272 automated unit, regression, and mathematical tests passing with zero failures.

---

## Honest Technical Boundaries & Operational Limitations

SyncWave prioritizes scientific honesty over marketing claims:

1. **No "Zero Latency" Claim**:
   - Heterogeneous output alignment requires delaying faster endpoints to match the slowest endpoint in the system ($\max_i(L_i)$). Total system playback delay equals the latency of the slowest active device (typically Bluetooth A2DP, which introduces $100\text{--}180\text{ ms}$ of buffer delay).
2. **Free-Air Bluetooth Earbud Acoustic Measurement Limitation (M11)**:
   - While DelayBuffer physical shifting is verified within $0.13\text{ ms}$ on local speakers, free-air acoustic arrival measurements over Bluetooth earbuds (such as realme Buds T310) exhibit high acoustic variance and noise sensitivity. In uncalibrated setups, SyncWave safely uses measured software driver latency.
3. **LibVLC Video-Clock Slaving Limitation (M13)**:
   - LibVLC 3.x does not expose an external display clock slaving callback to lock video presentation to external audio sample clocks. SyncWave measures and logs instantaneous A/V offset with microsecond precision, but does not manipulate LibVLC video frame dropping.

---

## Prerequisites & Building

### Prerequisites
- **Operating System**: Windows 10/11 (64-bit)
- **Compiler**: GCC 13+ / MinGW-w64 (UCRT64) or Visual Studio 2022+ (C++20 required)
- **Build System**: CMake 3.20+ with Ninja
- **Media Engine**: VLC Media Player 64-bit installed (Default: `C:\Program Files\VideoLAN\VLC`)

### Build Instructions (MinGW-w64 UCRT64 & Ninja)

```powershell
# Add toolchain and VLC to path
$env:PATH = "C:\Program Files\VideoLAN\VLC;C:\msys64\ucrt64\bin;$env:PATH"

# Configure Release build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build core library, CLI, and test runner
cmake --build build --config Release

# Run complete automated test suite (1,272 tests)
.\build\syncwave_tests.exe
```

---

## Example CLI Commands

### 1. Enumerate Audio Endpoints
```powershell
# List active output endpoints
.\build\syncwave.exe devices

# List all endpoints including disabled/unplugged
.\build\syncwave.exe devices --all
```

### 2. Multi-Endpoint Synthetic Tone Playback
```powershell
# Play 440 Hz tone across endpoints 2 and 3 with adaptive drift correction
.\build\syncwave.exe tone --outputs 2,3 --sync adaptive --duration 10
```

### 3. Capture & Route Windows System Audio (WASAPI Loopback)
```powershell
# Capture default Windows audio and route to Bluetooth Buds (2) and Realtek Speakers (3)
.\build\syncwave.exe capture --outputs 2,3 --sync adaptive --duration 30
```

### 4. Play Media Files via LibVLC Synchronization Engine
```powershell
# Play MP4 video/audio synchronized across 3 endpoints with telemetry logging
.\build\syncwave.exe media sample.mp4 --outputs 3,2,0 --sync adaptive --csv media_telemetry.csv
```

### 5. Multi-Device Long-Duration Stress Testing
```powershell
# Run 30-minute stress test with 1-second telemetry logging
.\build\syncwave.exe stress --outputs 3,2 --duration 1800 --csv stress_log.csv
```

---

## Public Engine API Boundary (GUI Readiness)

Future presentation layers (e.g., Qt, WinUI 3) interact exclusively through the abstract `ISyncWaveEngine` interface defined in [`src/core/ISyncWaveEngine.h`](file:///c:/Dev/Projects/SyncWave/src/core/ISyncWaveEngine.h):

```cpp
#include "core/ISyncWaveEngine.h"

auto engine = syncwave::createSyncWaveEngine();

// Enumerate available endpoints
auto outputs = engine->enumerateOutputDevices(true);

// Start synchronized media playback
engine->setSyncMode("adaptive");
engine->startMedia("movie.mp4", {"{device_id_1}", "{device_id_2}"});

// Query diagnostics snapshot at 10-30 Hz
auto diag = engine->getDiagnostics();
std::cout << "Process CPU: " << diag.system.processCpuPercent << "%\n";
std::cout << "Working Set: " << diag.system.memoryUsageMb << " MB\n";
```

Full GUI architecture and integration specifications are documented in [`docs/gui-api.md`](file:///c:/Dev/Projects/SyncWave/docs/gui-api.md).

---

## Testing & Quality Assurance

SyncWave includes 1,272 automated unit and integration tests covering:
- Lock-free ring buffer concurrency and boundary invariants
- Sinc interpolation micro-resampling rate stability
- Dual EMA drift estimator filtering and outlier rejection
- DriftController deadband, clamping, and disturbance recovery
- Media PTS $\leftrightarrow$ Master Frame bidirectional mapping
- Rapid seeking, pausing, and discontinuity flush integrity
- Multi-output disconnect/reconnect lifecycle safety
- Public engine API lifecycle and diagnostics query

To run tests:
```powershell
.\build\syncwave_tests.exe
```

---

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
