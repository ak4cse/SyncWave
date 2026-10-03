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
- [ ] **Milestone 3 — WASAPI Single Renderer & Test Tone**
- [ ] **Milestone 4 — WASAPI Loopback Capture**
- [ ] **Milestone 5 — Lock-Free Master Ring Buffer**
- [ ] **Milestone 6 — Multi-Endpoint Output Pipeline**
- [ ] **Milestone 7 — Physical Latency Measurement**
- [ ] **Milestone 8 — DelayBuffer & Static Synchronization**
- [ ] **Milestone 9 — Automatic Calibration**
- [ ] **Milestone 10+ — Clock Tracking & Drift Correction**

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

Example output:
```text
SyncWave Audio Devices
======================

[0] Speakers (Realtek(R) Audio)
    State: Active
    ID:    {0.0.0.00000000}.{e7d6dd87-9f7d-4ee3-9cff-762d87cf609d}

[1] Headphones (realme Buds T310) [DEFAULT]
    State: Active
    ID:    {0.0.0.00000000}.{cafb0285-e226-4a55-91f6-be05aa5e8347}

Total endpoints: 2
```

### Watch Audio Device Events in Real Time
```powershell
# Run device event monitor (hot-plug, default device change, disconnects)
.\build\syncwave.exe watch

# Or run with a timeout (in seconds)
.\build\syncwave.exe watch --timeout 10
```

### Running Tests
```powershell
.\build\syncwave_tests.exe
# or via CTest:
ctest --test-dir build --output-on-failure
```
