# Audio/Video Synchronization Architecture & Timeline Specification

## 1. Authoritative Clock Hierarchy

SyncWave separates media decoding from physical multi-endpoint audio rendering. The clock hierarchy is structured as follows:

```
                     MEDIA SOURCE (File / Container)
                                   │
               ┌───────────────────┴───────────────────┐
               │                                       │
         VIDEO STREAM                            AUDIO STREAM
               │                                       │
         VLC Demux/Decode                        VLC Demux/Decode
               │                                       │
    Video Presentation Clock                  libvlc Audio Callback (amem)
               │                                       │
        Direct3D / Display                      Decoded PCM (Float32 stereo)
                                                       │
                                                       ▼
                                           SyncWave Master Audio Bus
                                            (Authoritative Master Timeline)
                                                       │
                                                 OutputRouter
                                            ┌──────────┼──────────┐
                                            ▼          ▼          ▼
                                         Output A   Output B   Output C
                                            │          │          │
                                         Queue      Queue      Queue
                                            │          │          │
                                        Resampler  Resampler  Resampler
                                        (Drift)    (Drift)    (Drift)
                                            │          │          │
                                       DelayBuffer DelayBuffer DelayBuffer
                                         (Delay)    (Delay)    (Delay)
                                            │          │          │
                                         WASAPI     WASAPI     WASAPI
```

### Authoritative Clocks by Domain:
1. **Media Presentation**: The media container provides DTS/PTS timestamps.
2. **Synchronized Audio**: `MasterAudioBus` is the **authoritative clock** for multi-output playback. No individual output device or wireless Bluetooth clock is permitted to dictate the master audio timeline.
3. **Physical Audio Alignment**: `OutputRouter` applies DelayBuffer delays and micro-resampling to match all physical endpoints to the master timeline.
4. **A/V Alignment**: The difference between the visual video frame and the acoustic arrival represents the A/V synchronization offset.

---

## 2. Seven Distinct Timing Concepts

| Concept | Symbol | Units | Definition & Authoritative Source |
| :--- | :--- | :--- | :--- |
| **Media Presentation Timestamp** | $T_{\text{media}}$ | ms / $\mu\text{s}$ | Media position from start of container (VLC container demuxer). |
| **Source Audio Timestamp** | $PTS_{\text{audio}}$ | $\mu\text{s}$ | Presentation timestamp attached to raw decoded audio packets. |
| **Master Audio Timeline** | $T_{\text{master}}$ | frames / s | Continuous sample index $F_{\text{master}}$ in `MasterAudioBus`: $T_{\text{master}} = F_{\text{master}} / R_{\text{master}}$. |
| **Per-Output Software Timeline** | $T_{\text{sw}, i}$ | ms | Frames written to WASAPI buffer minus current padding frames. |
| **Device Hardware Clock** | $T_{\text{hw}, i}$ | ticks | Uninterrupted hardware counter sampled via `IAudioClock::GetPosition()`. |
| **Estimated Acoustic Arrival** | $T_{\text{phys}, i}$ | ms | Estimated wavefront arrival at listener: $T_{\text{sw}, i} + L_{\text{physical}, i}$. |
| **Video Presentation Timeline** | $T_{\text{video}}$ | ms | Presentation time of the video frame currently rendered to the display. |

---

## 3. A/V Offset Sign Convention

$$\text{A/V Offset} = T_{\text{audio\_perceived}} - T_{\text{video\_presented}}$$

- **Positive ($\text{A/V Offset} > 0$)**: **Audio leads Video**. Audio sounds before the corresponding visual cue appears (lips move after speech).
- **Negative ($\text{A/V Offset} < 0$)**: **Audio lags Video**. Visual cue appears before sound is heard (speech heard after lips move).
- **Zero ($\text{A/V Offset} = 0$)**: Lip-sync is perfectly locked.

---

## 4. Seek, Pause, and Discontinuity Semantics

### When Media Pauses:
- VLC halts decoded audio callbacks.
- `MasterAudioBus` stops receiving new frames.
- Output endpoints safely drain or hold buffers without declaring drift.
- **Drift controller is paused**: Drift regression ignores paused duration to avoid corrupting frequency slope estimates.

### When Media Seeks:
- VLC fires an audio `flush` event and clears its decoding pipeline.
- SyncWave performs immediate deterministic timeline re-anchoring:
  1. Stale audio packets in the bridge are discarded.
  2. `MasterAudioBus` and output queues are flushed.
  3. Master frame counter is re-associated with the new $T_{\text{media}}$ seek target.
  4. DelayBuffer delay allocations ($D_i$) and acoustic calibration profiles are **preserved** (a seek is a timeline jump, not a physical speaker distance change).
  5. Playback seamlessly resumes from the new seek position.

---

## 5. Public / Internal Timestamp Contract

```cpp
struct AudioTimestamp {
    uint64_t masterFrame = 0;       // Authoritative sample count in MasterAudioBus
    uint32_t sampleRate = 48000;    // Canonical sample rate (Hz)
    int64_t mediaTimestampUs = 0;   // Corresponding media presentation timestamp (microseconds)
    uint64_t qpcTimestamp = 0;       // Windows QPC high-resolution reference tick
    bool isDiscontinuity = false;    // True if block immediately follows a seek or reset
};
```
