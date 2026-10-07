# ADR 017: Audio/Video Synchronization Architecture & VLC Media Ingestion

## Status
Accepted

## Context
Across Milestones 1 through 12, SyncWave developed and certified an authoritative multi-device audio synchronization engine:
- `MasterAudioBus`: Authoritative digital PCM audio stream in canonical format (`Float32`, stereo, 48 kHz).
- `OutputRouter`: Concurrent routing across arbitrary physical WASAPI endpoints.
- `DelayBuffer`: Per-device millisecond delay compensation.
- `Resampler`: Pull-mode sinc/linear micro-resampling compensating individual crystal oscillator drift (PPM adjustments).
- `DriftController`: PI-feedback + linear regression frequency estimator keeping inter-device skew bounded over long durations (0 WASAPI underruns, 0 queue overruns).

Milestone 13 introduces **Audio/Video (A/V) Synchronization** and integrates VLC as an external media source. When SyncWave introduces intentional acoustic alignment delays (e.g. delaying fast speakers by 148 ms so they align with Bluetooth headphones) and micro-slews output clocks to maintain phase lock, the visual display (video frames) must remain synchronized with the perceived audio.

## Core Architectural Principles

1. **Hierarchy of Authoritative Clocks**:
   - Neither VLC's internal clock nor any Bluetooth/WASAPI hardware clock is permitted to dictate SyncWave's Master Audio Timeline.
   - The **Master Audio Timeline** remains the sole authoritative clock for synchronized audio dispatch.
   - Per-device delays ($D_i$) and micro-resampling rates compensate hardware/acoustic transport differences; they **never** shift the media timeline or the Master Audio Timeline.

2. **Decoupled Architecture**:
   VLC is utilized purely as an ingestion, decoding, demuxing, and video presentation engine.
   - Video is rendered by VLC.
   - Decoded PCM audio from VLC is captured via `libvlc_audio_set_callbacks` (`amem` audio output) in `Float32` stereo format.
   - Decoded audio blocks with presentation timestamps (PTS) are bridged into SyncWave's `MasterAudioBus`.
   - Real-time callbacks never perform file I/O, console logging, or blocking operations.

3. **Timeline Abstraction & Separation of Concepts**:
   We explicitly distinguish 7 timelines:
   1. **Media Presentation Timestamp ($T_{\text{media}}$)**: Position in the source file/stream (seconds / microseconds from container start).
   2. **Source Audio Timestamp ($PTS_{\text{audio}}$)**: Presentation timestamp stamped on decoded audio chunks by the demuxer.
   3. **Master Audio Timeline ($T_{\text{master}}$)**: Authoritative continuous sample counter $F_{\text{master}}$ on `MasterAudioBus`:
      $$T_{\text{master}} = \frac{F_{\text{master}}}{R_{\text{master}}}$$
   4. **Per-Output Software Playback Timeline ($T_{\text{out}, i}$)**: Frames submitted to WASAPI minus current hardware buffer padding.
   5. **Device Hardware Clock**: `IAudioClock` position sampled from endpoint hardware.
   6. **Estimated Physical/Acoustic Arrival**: Physical wavefront arrival at the listener's ear ($T_{\text{phys}, i} = T_{\text{out}, i} + L_{\text{physical}, i}$).
   7. **Video Presentation Timeline ($T_{\text{video}}$)**: Current presentation timestamp of displayed video frames reported by VLC.

4. **A/V Sync Offset Definition & Sign Convention**:
   We define:
   $$\text{A/V Offset} = T_{\text{audio\_perceived}} - T_{\text{video\_presented}}$$
   - **Positive ($\text{A/V Offset} > 0$)**: Audio is **leading (ahead of)** video. Video looks delayed.
   - **Negative ($\text{A/V Offset} < 0$)**: Audio is **lagging (behind)** video. Video looks rushed.
   - For an uncompensated stream: $T_{\text{audio\_perceived}} \approx T_{\text{master}} - \bar{L}_{\text{output}}$.

5. **Discontinuity and Seek Semantics**:
   - `libvlc` signals timeline resets via the `flush` callback and `set_time` commands.
   - Upon seek or discontinuity:
     1. Stop accepting stale audio packets.
     2. Flush `MasterAudioBus` and per-endpoint output queues.
     3. Re-anchor the media-to-master frame mapping ($T_{\text{media}} \mapsto F_{\text{master}}$).
     4. Maintain physical delay calibration settings without recalculation (seek is a timeline jump, not a hardware calibration change).
     5. Pause does NOT corrupt drift regression: drift estimation is paused while media is paused.

## Consequences & Guarantees
- Zero modification to M12 drift controller or pull-mode resampler.
- All 1,179 automated unit tests remain untouched and green.
- Deterministic synthetic test harness isolates timeline conversion and discontinuity logic prior to hardware tests.
