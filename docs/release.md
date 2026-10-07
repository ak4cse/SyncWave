# SyncWave Release Procedure & Operational Runbook

This document describes the environment setup, dependency management, build procedures, release tagging, and verification guidelines for SyncWave.

---

## 1. System Requirements & Architecture

- **Operating System**: Windows 10 (Build 19041+) or Windows 11 (x64)
- **Audio Subsystem**: Windows Core Audio / WASAPI (Shared Event-Driven & Loopback Capture)
- **Media Engine**: VideoLAN LibVLC 3.0.x (x64)
- **Compiler Support**: Modern C++20 compliant compiler (GCC 13+ via MinGW-w64 UCRT64, or MSVC 2022 v17.4+)
- **Build System**: CMake 3.20+ with Ninja or Visual Studio generators

---

## 2. Dependencies & Acquisition

SyncWave avoids vendoring proprietary or copyrighted binaries into version control.

### 2.1 LibVLC (Media Extraction)
SyncWave uses `libvlc.dll` to demux container files (MP4, MKV, WebM, etc.) and stream decoded audio via memory callbacks (`amem`) into SyncWave's authoritative master bus.

1. **Install VLC Media Player (64-bit)**:
   - Official installer from [videolan.org](https://www.videolan.org/vlc/) (Standard default path: `C:\Program Files\VideoLAN\VLC`).
2. **Import Library Generation**:
   SyncWave includes official LibVLC API headers in `third_party/libvlc/include/vlc/`. The MinGW-w64 import library is generated directly from the installed DLL:
   ```powershell
   cd third_party\libvlc\lib
   gendef "C:\Program Files\VideoLAN\VLC\libvlc.dll"
   dlltool -d libvlc.def -l libvlc.dll.a -D libvlc.dll -m i386:x86-64
   ```
3. **Runtime Execution**:
   Ensure `C:\Program Files\VideoLAN\VLC` is present in the system or session `PATH`:
   ```powershell
   $env:PATH = "C:\Program Files\VideoLAN\VLC;$env:PATH"
   ```

### 2.2 Windows System Libraries
Linked directly via CMake (`CMakeLists.txt`):
- `ole32`, `oleaut32`, `propsys` (COM & Endpoint Properties)
- `avrt` (Multimedia Class Scheduler Service - MMCSS "Pro Audio")
- `mfplat`, `ksuser`, `winmm` (Multimedia timers & formats)
- `psapi` (Process performance & memory diagnostics)

---

## 3. Clean Build Procedure

### 3.1 MinGW-w64 (UCRT64) with Ninja (Recommended)
```powershell
# Set path to toolchain and VLC
$env:PATH = "C:\Program Files\VideoLAN\VLC;C:\msys64\ucrt64\bin;$env:PATH"

# Configure Release build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Compile binaries
cmake --build build --config Release

# Run automated test suite
.\build\syncwave_tests.exe
```

### 3.2 Microsoft Visual Studio 2022
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\syncwave_tests.exe
```

---

## 4. Release Checklist & Git Workflow

Before creating a GitHub release or cutting a version tag:

1. **Working Tree Cleanliness**:
   - Ensure `git status` shows no uncommitted code or tracked binary artifacts (`*.exe`, `*.obj`, `*.dll.a`, `*.csv`).
2. **Automated Verification**:
   - Run `syncwave_tests.exe`. All 1,272+ automated unit and integration tests must pass with zero failures.
3. **Physical Smoke Test**:
   - Verify active audio enumeration: `syncwave devices`
   - Verify tone routing across active endpoints: `syncwave tone --outputs 0,1 --duration 2`
   - Verify media load failure resilience: `syncwave media non_existent.mp4`
4. **Tagging Convention**:
   - Use semantic versioning: `v0.MAJOR.MINOR` (e.g. `v0.13.0`, `v0.14.0`).
   - Create annotated tag:
     ```powershell
     git tag -a v0.14.0 -m "Release v0.14.0: Production hardening and public engine API"
     git push origin main
     git push origin v0.14.0
     ```

---

## 5. Known Operational Boundaries & Honest Disclaimers

1. **Acoustic Calibration on Bluetooth (M11 Boundary)**:
   - DelayBuffer physical hardware shifting is validated within sub-millisecond precision on local speakers.
   - However, free-air acoustic arrival measurement over Bluetooth earbuds (such as realme Buds T310) exhibits high acoustic variance and ambient noise sensitivity. Uncalibrated Bluetooth devices default to estimated software latency.
2. **VLC Video Presentation Clock (M13 Boundary)**:
   - LibVLC 3.x does not expose an external display clock slaving interface. SyncWave measures and logs the instantaneous A/V offset with microsecond precision, but does not manipulate LibVLC display frame dropping.
