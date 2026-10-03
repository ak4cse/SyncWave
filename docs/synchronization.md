# SyncWave Synchronization Model & Architecture

## Overview
SyncWave synchronizes real-time audio across heterogeneous Windows audio render endpoints (such as Bluetooth headphones, USB DACs, HDMI displays, and built-in Realtek speakers). Heterogeneous endpoints present two fundamental synchronization challenges:
1. **Static Latency Differences**: Hardware buffers, Bluetooth A2DP transport latency, OS mixing delays, and DAC buffering cause different endpoints to lag behind one another by tens to hundreds of milliseconds.
2. **Dynamic Clock Drift**: Each audio endpoint runs on its own independent hardware oscillator or crystal. Over time, clock frequencies deviate by several parts per million (PPM), causing samples to accumulate or deplete relative to the master timeline.

---

## Milestone 6 Status: Multi-Output Fan-Out (No Physical Sync Yet)

> [!IMPORTANT]
> **Milestone 6 does NOT claim or implement physical acoustic synchronization, latency calibration, delay alignment, or clock drift compensation.**

In Milestone 6:
- Audio frames captured from Windows system audio (or generated via `ToneGenerator`) are dispatched from the central `MasterAudioBus` by `OutputRouter`.
- `OutputRouter` fans out identical frames to each active endpoint's dedicated SPSC queue.
- Each endpoint's `WasapiOutput` thread pulls and renders audio at its own native hardware rate and clock pace.
- While audio is rendered concurrently across all configured outputs, playback timing is governed solely by the respective endpoint's native hardware latency and uncompensated clock.
- Endpoints will have noticeable phase/timing offsets (e.g. Bluetooth audio lagging ~100–200 ms behind internal speakers).

---

## Planned Synchronization Roadmap (Milestones 7+)

1. **Milestone 7: Shared Timeline & Latency Calibration**:
   - Master reference clock timeline established.
   - Per-endpoint latency measurement (via loopback calibration, acoustic chirp analysis, or empirical WASAPI clock querying).
   - Insertion of adjustable delay lines (`DelayBuffer`) into each `DeviceOutput` queue to align playhead arrival times across endpoints.

2. **Milestone 8: Clock Drift Tracking & Dynamic Pitch/Rate Correction**:
   - Continuous tracking of endpoint clock position via `IAudioClock::GetPosition`.
   - Estimation of relative clock drift (ppm) relative to the master clock.
   - Dynamic resampler ratio modulation or phase vocoding / time-stretching to prevent buffer underrun/overrun without audible clicks or pitch warble.

3. **Milestone 9: Sub-Millisecond Phase Alignment**:
   - Dynamic phase compensation and jitter absorption filters.
   - Real-time phase alignment for multi-room and surround sound coherence.
