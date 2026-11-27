# SyncWave

**Synchronized multi-device audio routing for Windows.**  
Play movies in VLC, stream YouTube, or listen to Spotify across wired speakers and multiple Bluetooth headphones at the exact same time — in sync, with independent hardware volume per listener.

---

## Why SyncWave?

### The Problem
Ever tried watching a movie or video on a laptop with a friend where one person wants Bluetooth earphones and the other uses wired speakers (or a second pair of headphones)?

On Windows, three things break immediately:
1. **Single-output limitation**: Windows only routes audio to one default playback device at a time.
2. **Bluetooth latency & echo**: Even with virtual audio cables, Bluetooth A2DP headphones have ~150–250 ms of wireless transport and DSP buffering delay compared to wired speakers — creating an unbearable echo.
3. **Clock drift & shared volume**: Independent audio hardware crystals drift apart over a 2-hour movie, and Windows has no clean way for each person to control their own hardware volume comfortably.

### Our Solution
**SyncWave** solves this with a real-time C++20 WASAPI audio engine and a lightweight WebView2 desktop UI:
- **Zero-setup System Audio Capture**: Click **Start Sync** and SyncWave automatically switches Windows' default playback device to a loopback carrier, captures all system/browser/VLC audio, and automatically restores your original default device when you click **Stop** or close the app.
- **Per-Device Hardware Volume (`0%–100%`)**: Uses Windows Core Audio `IAudioEndpointVolume` so each listener controls their own true hardware master volume (defaulting to a safe 50% on first launch and remembering your levels automatically).
- **Per-Device Live Sync Delay (`0–400 ms`)**: Uses a lock-free circular `DelayBuffer` so wired speakers can be delayed smoothly in real time to lock with Bluetooth headphones.
- **Closed-Loop Clock Drift Correction**: Continuously estimates each endpoint's hardware clock rate (`DeviceClock` + `FilteredDriftEstimator`) and applies slew-limited sinc micro-resampling (`±100 ppm`) so devices stay locked over long movies.
- **Customizable Studio UI**: Choose between **Brutalist**, **Studio**, or **Minimal** UI styles in **AMOLED Dark** or **Crisp Light** mode, with a built-in quick-start guide and an optional collapsible diagnostics drawer.

---

## Quick Start (Users)

1. Run the Windows installer **`SyncWave-Setup-1.0.0.exe`** (installs to `Program Files\SyncWave`, adds a Desktop shortcut, and registers SyncWave in your Start Menu / Apps list).
2. Open **SyncWave** and toggle **ON** the speakers/headphones you want to sync.
3. Click **Start Sync**, then play your movie in **VLC**, **Chrome**, **YouTube**, or **Spotify**.
4. Adjust each device's **Master Volume** and **Sync Delay** sliders live until everything sounds locked.
   - *Watching in external VLC?* If Bluetooth latency puts audio behind video on screen, SyncWave shows the exact `J` hotkey offset (e.g. `-200 ms`) to align VLC's video lip-sync.

---

## Building from Source & Creating the Installer (Windows / MSYS2 UCRT64)

### Prerequisites
- **MSYS2 UCRT64** (`cmake`, `ninja`, `g++` C++20, `mingw-w64-ucrt-x86_64-webview2-loader`)
- **Inno Setup 6** (`winget install JRSoftware.InnoSetup`) for building the Windows installer
- **Edge WebView2 Runtime** (pre-installed on Windows 10/11)
- **VLC 64-bit** (`C:\Program Files\VideoLAN\VLC`) for LibVLC headers/DLLs

### Build & Run Tests
```powershell
$env:PATH = "C:\Program Files\VideoLAN\VLC;C:\msys64\ucrt64\bin;$env:PATH"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
.\build\syncwave_tests.exe
```

### Build Single-File Windows Installer (`SyncWave-Setup-1.0.0.exe`)
```powershell
powershell -ExecutionPolicy Bypass -File .\installer\build_installer.ps1
```
This stages `syncwave_gui.exe`, `syncwave.exe`, and all required runtime DLLs into `dist\stage\` and compiles `dist\SyncWave-Setup-1.0.0.exe` with Desktop + Start Menu shortcuts and a Windows uninstaller.

---

## Project Structure
- `src/audio/` — `MasterAudioBus`, `OutputRouter`, `DeviceOutput`, `AudioEngine`, `RingBuffer`
- `src/sync/` — `DeviceClock`, `FilteredDriftEstimator`, `SyncController`, `DelayBuffer`, `AcousticCalibrator`
- `src/dsp/` — `Resampler`, `ChirpGenerator`, `CorrelationDetector`
- `src/windows/` — `DeviceManager`, `WasapiOutput`, `WasapiCapture`, `DefaultAudioSwitcher`, `EndpointVolumeController`
- `src/av/` — `VlcMediaEngine`, `TimelineModel`
- `src/core/` — `ISyncWaveEngine`, `SyncWaveEngineImpl`, `DiagnosticsSnapshot`
- `src/gui/` — WebView2 native host (`MainWindow.h/.cpp`) & embedded UI (`StudioUiHtml.h`)
- `src/app/` — CLI interface (`syncwave.exe`)
- `installer/` — Inno Setup 6 script (`SyncWave.iss`) & packaging script (`build_installer.ps1`)
- `tests/` — Automated unit & integration test suite (`syncwave_tests.exe`)

---

## Credits

Developed by **Ankush** ([ak4cse](https://github.com/ak4cse)).
