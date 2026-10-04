# SyncWave

**SyncWave** is a Windows real-time heterogeneous audio synchronization engine written in modern C++ (C++20).

SyncWave captures system audio (via WASAPI loopback) and renders it concurrently across multiple independent physical audio endpoints (Bluetooth earbuds, Bluetooth speakers, USB headsets, wired headphones, HDMI audio) while measuring and compensating for physical acoustic latency, buffering differences, and clock drift.

---

## Architecture Principles

1. **CLI First**: The audio engine and synchronization subsystems are developed and verified using command-line tools and reproducible experiments before adding any graphical interface.
2. **Strict Separation of Planes**:
   - **Audio Data Plane**: Real-time lock-free PCM movement (Capture -> RingBuffer -> DelayBuffer -> Renderer). No allocations, no mutexes, no console I/O in the render callback.
   - **Timing Plane**: Logical master clock, endpoint clock monitoring, empirical latency estimation, and drift estimation.
   - **Control Plane**: Device management, configuration, and user commands.
3. **Empirical Measurement over Assumption**: No hardcoded latency or drift values; delays are calculated from direct measurement.

---

## Project Status

- [x] **Milestone 1 — Device Enumeration & CLI Foundation**
  - Native Windows Core Audio endpoint enumeration (`IMMDeviceEnumerator`, `IMMDeviceCollection`).
  - Friendly name resolution, device state tracking (Active, Disabled, Not Present, Unplugged), default endpoint identification.
  - Decoupled `CommandInterface` and `DeviceManager` abstractions.
  - Test suite with string/HRESULT unit tests and hardware integration tests.
  - CLI command: `syncwave devices [--all]`
- [x] **Milestone 2 — Hot-Plug & Device Notifications** (`IMMNotificationClient`)
  - Real-time detection of endpoint added, removed, state changed, default changed, and property changed.
  - Atomic COM reference counting and thread-safe MTA callback handling.
  - Decoupled `DeviceEvent` queue avoiding COM dependencies in CLI.
  - CLI command: `syncwave watch [--timeout <sec>]`
- [x] **Milestone 3 — WASAPI Single Renderer & Test Tone**
  - Native WASAPI shared event-driven audio renderer (`WasapiOutput`).
  - High-priority MMCSS ("Pro Audio") render thread with zero busy-waiting.
  - Pure mathematical `ToneGenerator` supporting Float32 / PCM with continuous phase tracking.
  - Hardware clock tracking via `IAudioClock`.
  - CLI command: `syncwave tone [--device <id|index>] [--frequency <Hz>] [--duration <sec>] [--volume <0..1>]`
- [x] **Milestone 4 — Master Audio Bus & Real-Time Ring Buffer**
  - Lock-free Single-Producer / Single-Consumer (SPSC) circular buffer (`RingBuffer`).
  - Central canonical audio timeline (`MasterAudioBus`) with bounded preallocated storage.
  - Decoupled pipeline: `ToneGenerator -> MasterAudioBus -> WasapiOutput`.
  - CLI commands: `syncwave tone`, `syncwave status`.
- [x] **Milestone 5 — WASAPI Loopback Capture & Real-Time Resampling**
  - Native WASAPI loopback capture client (`WasapiCapture`) on render endpoints.
  - High-priority MMCSS ("Capture") thread with silence packet translation.
  - Output-boundary linear interpolation sample rate converter (`Resampler`).
  - Live system audio pipeline: `Windows Audio -> WASAPI Loopback -> MasterAudioBus -> Resampler -> WasapiOutput -> Hardware`.
  - CLI command: `syncwave capture [--source <id|index>] [--output <id|index>] [--duration <sec>]`
- [x] **Milestone 6 — Multiple Simultaneous WASAPI Outputs & OutputRouter**
  - Dedicated fan-out router (`OutputRouter`) acting as sole consumer of `MasterAudioBus`.
  - Independent per-output pipelines (`DeviceOutput`) with isolated 1.0s SPSC queues, dedicated resamplers, and MMCSS render threads.
  - Non-destructive queue overflow protection and hot-unplug tolerance.
  - Concurrent multi-endpoint rendering across heterogeneous devices (e.g. 48.0 kHz Realtek + 44.1 kHz Bluetooth realme Buds T310).
  - CLI commands: `syncwave tone --outputs 2,3`, `syncwave capture --outputs 2,3`
- [x] **Milestone 7 — Timing & Clock Measurement Infrastructure**
  - Low-level WASAPI hardware clock querying (`IAudioClock`, `IAudioClient::GetCurrentPadding`, `GetStreamLatency`).
  - Sliding-window Ordinary Least Squares (OLS) linear regression clock rate modeling (`DeviceClock`) reporting effective rate ($Hz$) and nominal error ($ppm$).
  - Pairwise relative drift estimation (`DriftEstimator`) computing rate ratios, relative drift ($ppm$), and playhead offset ($ms$).
  - Non-intrusive 10 Hz out-of-band sampling maintaining zero contention and real-time safety on WASAPI render threads.
  - Physical acoustic latency vs software latency domain distinction.
- [x] **Milestone 8 — Relative Latency & Timing Model Validation**
  - Multi-window clock convergence analysis (`DeviceClock::estimateRateOverWindow`) resolving the Bluetooth startup ramp artifact.
  - Mathematical separation of instantaneous offset ($T_A - T_B$) from accumulated drift ($\Delta\text{Offset}$) and drift rate ($ppm$).
  - End-to-end stream playhead estimation (`estimatedAppPlayheadFrames/Sec`, `wasapiClockPlayheadSec`, `playheadDiscrepancyMs`).
  - Deterministic transient pulse generator (`SyncPulseGenerator`) for repeatable software latency evaluation.
  - High-precision 60s clock experiment: `syncwave clock-test --outputs 2,3 --duration 60`
  - Deterministic transient latency experiment: `syncwave latency-test --outputs 2,3 --runs 5`
  - Honest physical acoustic latency assessment distinguishing software driver latency from physical acoustic emission.
  - Expanded test suite: 422 passing automated unit and integration tests.
- [ ] **Milestone 9 — DelayBuffer & Static Delay Alignment**
- [ ] **Milestone 10 — Dynamic Drift Correction**
- [ ] **Milestone 11 — Automatic Calibration**

---

## Prerequisites & Building

### Prerequisites
- Windows 10/11
- CMake 3.20+
- C++20 compatible compiler (MSVC 2022+ or GCC 13+ / MinGW-w64 UCRT64)
- Ninja or MSBuild

### Build Instructions

Using Ninja & GCC / UCRT64:
```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Using Visual Studio:
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

---

## Running

### Enumerate Audio Devices
```powershell
# List active output endpoints
.\build\syncwave.exe devices

# List all endpoints including unplugged/disabled devices
.\build\syncwave.exe devices --all
```

### Watch Audio Device Events in Real Time
```powershell
# Run device event monitor (hot-plug, default device change, disconnects)
.\build\syncwave.exe watch

# Or run with a timeout (in seconds)
.\build\syncwave.exe watch --timeout 10
```

### Capture Windows System Audio via WASAPI Loopback
```powershell
# Capture from default render endpoint and play through default output for 5 seconds
.\build\syncwave.exe capture

# Capture from Realtek Speakers [3] and route concurrently to Bluetooth Buds [2] and Realtek [3]
.\build\syncwave.exe capture --source 3 --outputs 2,3 --duration 10

# Repeated -o / --output syntax is also supported
.\build\syncwave.exe capture --source 3 -o 2 -o 3 --duration 5

# Continuous multi-device streaming until Ctrl+C
.\build\syncwave.exe capture --source 3 --outputs 2,3 --duration 0
```

### Play Synthetic Test Tone
```powershell
# Play 440 Hz test tone on default output for 5 seconds
.\build\syncwave.exe tone

# Play simultaneously across multiple endpoints
.\build\syncwave.exe tone --outputs 2,3 --duration 5

# Play on specific device index with custom parameters
.\build\syncwave.exe tone -o 2 --frequency 440 --duration 10 --volume 0.25

# Play continuous tone across multiple endpoints until Ctrl+C
.\build\syncwave.exe tone --outputs 2,3 --duration 0
```

### High-Precision 60-Second Clock Stability Experiment
```powershell
# Run continuous 60s experiment with multi-window convergence table (1s, 5s, 10s, 30s, 60s)
.\build\syncwave.exe clock-test --outputs 2,3 --duration 60
```

### Deterministic Transient & Relative Latency Experiment
```powershell
# Run 5 repeated impulse runs to evaluate software-path latency repeatability
.\build\syncwave.exe latency-test --outputs 2,3 --runs 5
```

### Check Status & Telemetry
```powershell
.\build\syncwave.exe status
```

### Running Tests
```powershell
.\build\syncwave_tests.exe
# or via CTest:
ctest --test-dir build --output-on-failure
```
