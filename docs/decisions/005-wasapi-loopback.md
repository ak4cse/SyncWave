# ADR 005: WASAPI Loopback Capture & Real-Time Resampling

## Context
In Milestones 1–4, SyncWave relied on a synthetic sine wave generator (`ToneGenerator`) to validate output rendering and circular buffering. To fulfill the core mission of SyncWave—synchronizing real Windows audio across multiple heterogeneous endpoints—the engine must capture live system and application audio directly from Windows Core Audio via WASAPI Loopback.

## Decision
We implemented native WASAPI loopback capture (`WasapiCapture`), integrated it into `AudioEngine`, and introduced a real-time linear resampler (`Resampler`) to bridge sample rate differences between heterogeneous capture and render endpoints:

1. **WASAPI Loopback Capture Architecture (`WasapiCapture`)**:
   - Captures system audio from an `eRender` endpoint using `AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK` in `AUDCLNT_SHAREMODE_SHARED`.
   - Uses native `IAudioCaptureClient` to drain packets on Win32 event signals (`hAudioEvent`) with an MMCSS-boosted capture thread (`L"Capture"` / `L"Audio"`).
   - Handles `AUDCLNT_BUFFERFLAGS_SILENT` by delivering zero-filled Float32 silence frames.
   - Converts non-Float32 capture streams (such as Int16 or Int32 PCM) into canonical Float32 on the fly.
   - Comprehensive telemetry tracking frames captured, packets captured, silence packets, discontinuities, and capture errors.

2. **Master Format & Pipeline Routing**:
   - The native capture stream format defines the sample rate and channel layout of `MasterAudioBus`.
   - `MasterAudioBus` holds canonical Float32 stereo PCM with a 1.0-second buffer capacity.
   - The decoupled real-time pipeline is:
     `Windows Audio -> WASAPI Loopback -> MasterAudioBus -> (Resampler) -> WasapiOutput -> Hardware`

3. **Output-Boundary Resampler (`Resampler`)**:
   - Resampling is performed near the output boundary rather than mutating the master bus.
   - Uses fractional-phase linear interpolation with sample boundary caching (`lastSample_`) across chunk edges, ensuring bit-exact phase continuity and zero audible clicks.
   - Provides a `pull(MasterAudioBus&, float*, size_t)` interface backed by a circular FIFO, guaranteeing that the downstream `WasapiOutput` render client receives the exact number of frames requested every callback period.

4. **Milestone 4 Audit Resolution**:
   - In Milestone 4, the unthrottled synthetic tone producer filled the 1.0-second ring buffer to capacity (44,100 frames ahead of playhead), leaving ~43,600 frames unconsumed when playback abruptly stopped.
   - With real WASAPI loopback capture, the producer is driven by the physical Windows audio clock, ensuring produced frame rates match consumption at 1x real-time rate.

## Consequences
- SyncWave now streams real application audio (YouTube, Spotify, games, system sounds) without synthetic tone generators.
- Both integrated audio (Realtek) and Bluetooth (realme Buds T310) can act as independent capture sources and render destinations.
- Prepares SyncWave for Milestone 6: Concurrent Multi-Device Output Rendering.
