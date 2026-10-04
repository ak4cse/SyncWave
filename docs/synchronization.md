# SyncWave Synchronization Model & Architecture

## Overview
SyncWave synchronizes real-time audio across heterogeneous Windows audio render endpoints (such as Bluetooth headphones, USB DACs, HDMI displays, and built-in Realtek speakers). Heterogeneous endpoints present two fundamental synchronization challenges:
1. **Static Latency Differences**: Hardware buffers, Bluetooth A2DP transport latency, OS mixing delays, and DAC buffering cause different endpoints to lag behind one another by tens to hundreds of milliseconds.
2. **Dynamic Clock Drift**: Each audio endpoint runs on its own independent hardware oscillator or crystal. Over time, clock frequencies deviate by several parts per million (PPM), causing samples to accumulate or deplete relative to the master timeline.

---

---

## Core Timing Concepts & Taxonomy

SyncWave maintains strict distinctions between the following timing concepts:

### 1. Timing Dimensions
- **Latency**: Total transit time (in milliseconds) from audio generation or capture to physical acoustic emission.
- **Offset**: Fixed time difference (in milliseconds) between the playheads of two audio streams at a given instant ($T_A - T_B$).
- **Drift**: Continuous rate of divergence over time (measured in parts per million, **ppm**) caused by differing hardware oscillator frequencies:
  $$\text{ppm} = \left(\frac{R_{\text{obs}}}{R_{\text{nom}}} - 1\right) \times 10^6$$

### 2. Timing Domains & Clock References
- **SyncWave Logical Timeline**: Logical frame counter advanced by `MasterAudioBus`.
- **WASAPI Stream Position**: Frame playhead reported by `IAudioClock::GetPosition` paired with hardware clock frequency and QPC correlation.
- **WASAPI Buffer Padding**: Queued frames in the Windows audio engine mixer buffer (`IAudioClient::GetCurrentPadding`).
- **Physical Acoustic Emission**: Actual moment sound waves radiate from speakers or headphones.

> [!WARNING]
> **Acoustic Latency vs Software Timestamps**:
> Software timestamps, QPC values, and `IAudioClient::GetStreamLatency` measure only the OS and driver buffering layers. In Windows event-driven shared mode, `GetStreamLatency` reports `0 ms` because buffers are driven by event callbacks.
> Software clocks **do not and cannot measure physical acoustic latency**, which includes Bluetooth A2DP packetization, RF transmission, DAC anti-aliasing filter delay, and transducer mechanical inertia. Physical acoustic alignment requires external acoustic measurement (e.g. chirp analysis) planned for subsequent milestones.

---

## Milestone 7 Status: Timing & Clock Measurement Infrastructure

> [!IMPORTANT]
> **Milestone 7 strictly measures and models timing. It does NOT apply automatic delay compensation or drift correction.**

### Implemented Subsystems:
1. **`DeviceClock`**:
   - Maintains a thread-safe sliding window (128 samples) of valid `WasapiClockSnapshot` records sampled at 10 Hz out-of-band.
   - Computes effective clock rates ($Hz$) and nominal frequency error ($ppm$) using Ordinary Least Squares (OLS) linear regression:
     $$\text{Estimated Rate } (\text{Hz}) = \frac{\sum (t_i - \bar{t})(p_i - \bar{p})}{\sum (t_i - \bar{t})^2}$$
   - Evaluates goodness of fit ($r^2$) to verify clock stability.
2. **`DriftEstimator`**:
   - Calculates normalized clock rate ratios and pairwise relative drift ($ppm$):
     $$\text{Relative Drift (ppm)} = \left(\frac{r_A}{r_B} - 1\right) \times 10^6, \quad \text{where } r = \frac{R_{\text{obs}}}{R_{\text{nom}}}$$
   - Calculates instantaneous relative playhead offset ($T_A - T_B$) in milliseconds.
3. **Telemetry & Diagnostics**:
   - Exposed through `DeviceOutputTelemetry` and CLI reporting across `tone` and `capture` commands.

### Verified Hardware Experiment:
- Dual-playback session running 30 seconds across `Speakers (Realtek(R) Audio)` (48 kHz) and `Headphones (realme Buds T310)` (44.1 kHz Bluetooth):
  - **Realtek Integrated Clock**: 47,999.5 Hz (-11.1 ppm)
  - **realme Buds Bluetooth Clock**: 48,000.3 Hz (+7.1 ppm)
  - **Pairwise Relative Drift**: +18.18 ppm
  - **Instantaneous Offset**: 27.8 ms

---

## Planned Synchronization Roadmap (Milestones 8+)

1. **Milestone 8: Delay Buffer & Static Latency Alignment**:
   - Insertion of adjustable delay lines (`DelayBuffer`) into each `DeviceOutput` queue.
   - Aligning playheads by delaying faster devices to match slowest device ($T_{\text{target}} = \max(\text{latency}) + \text{margin}$).

2. **Milestone 9: Dynamic Drift Correction**:
   - Continuous drift tracking via `DriftEstimator`.
   - Micro-resampling / dynamic clock rate modulation to keep drift within sub-millisecond bounds without buffer underruns or pitch artifacts.

3. **Milestone 10: Automatic Calibration**:
   - Acoustic chirp generation and loopback capture for true physical latency discovery.

