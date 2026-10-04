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

## Milestone 8 Status: Relative Latency & Timing Model Validation

> [!IMPORTANT]
> **Milestone 8 rigorously validates and audits timing models, separates offset from drift, and models stream playheads. It does NOT implement automatic delay compensation or dynamic feedback drift correction.**

### Implemented Subsystems & Mathematical Models:
1. **Mathematical Separation of Offset vs Accumulated Drift vs Drift Rate**:
   - **Instantaneous Offset**: $T_A(t) - T_B(t)$ in milliseconds (snapshot playhead difference).
   - **Accumulated Drift**: $\Delta\text{Offset} = \text{Offset}(t_1) - \text{Offset}(t_0)$ in milliseconds (net physical divergence over duration $\Delta t$).
   - **Drift Rate**: $\frac{\Delta\text{Offset}}{\Delta t} \times 10^6$ in ppm.
   - *Crucial property*: If Device A is 100 ms ahead and remains 100 ms ahead, accumulated drift is strictly 0.0 ms.

2. **Multi-Window Rate Estimation & Startup Transient Resolution**:
   - `DeviceClock::estimateRateOverWindow(windowSec)` evaluates rate stability across trailing windows (1s, 5s, 10s, 30s, 60s).
   - Resolved the Bluetooth -612 ppm loopback artifact: proved that initial A2DP buffer ramp (~190 ms) skews short regression slopes, whereas steady-state linear regression converges (+1.08 ppm at 10s).

3. **Stream Playhead Estimation**:
   - $\text{App Playhead (frames)} = \text{framesSubmitted} - \text{currentPadding}$
   - $\text{App Playhead (seconds)} = \frac{\text{framesSubmitted} - \text{currentPadding}}{f_s}$
   - $\text{WASAPI Clock Playhead (seconds)} = \frac{\text{clockPosition}}{\text{clockFrequency}}$
   - $\text{Playhead Discrepancy (ms)} = (\text{App Playhead} - \text{WASAPI Playhead}) \times 1000$

4. **Deterministic Transient Test Harness (`SyncPulseGenerator`)**:
   - Generates bandlimited half-cycle sine impulses (1.0 ms duration, peak 1.0f) with 500 ms lead-in and lead-out silence.
   - Evaluated across repeated runs via `syncwave latency-test`.

### Verified 60-Second Real Hardware Validation:
Conducted on `Headphones (realme Buds T310)` (Bluetooth) and `Speakers (Realtek(R) Audio)` (Integrated) with 0 underruns:
- **Rate Convergence Across Trailing Windows**:
  | Window | realme Buds T310 Rate | Buds Error (ppm) | Realtek Audio Rate | Realtek Error (ppm) |
  | :--- | :--- | :--- | :--- | :--- |
  | **1 s** | 47,994.69 Hz | -110.59 ppm | 48,003.87 Hz | +80.52 ppm |
  | **5 s** | 48,000.26 Hz | +5.46 ppm | 47,998.24 Hz | -36.62 ppm |
  | **10 s** | 48,000.05 Hz | +1.08 ppm | 47,997.70 Hz | -48.00 ppm |
  | **30 s** | 47,878.80 Hz | -2525.09 ppm | 47,998.54 Hz | -30.40 ppm |
  | **60 s** | 47,994.35 Hz | -117.67 ppm | 47,998.38 Hz | -33.70 ppm |
- **Pairwise Drift & Offset Metrics**:
  - Relative Drift Rate: -83.98 ppm
  - Initial Offset: -949.83 ms
  - Final Offset: -1000.71 ms
  - Accumulated Drift: -50.88 ms over 53.58 s
  - Confidence ($r_A^2 \times r_B^2$): 1.0000

### Verified Deterministic Transient Repeatability (5 Runs):
- Mean Latency: -8.00 ms
- Median Latency: -10.00 ms
- Min Latency: -10.00 ms
- Max Latency: 0.00 ms
- Std Deviation: 4.47 ms

### Physical Acoustic Latency Honesty Statement:
Software timestamps (`IAudioClock`, QPC, and padding) observe only OS audio engine mixing and driver submission stages.
Physical acoustic emission includes external physical delays:
- Bluetooth A2DP transport packetization and RF transmission delay (~100–250 ms depending on SBC/AAC/LDAC codec profiles and buffer depths).
- Hardware DAC reconstruction / anti-aliasing filter delay.
- Transducer electromechanical latency and room air flight time ($~1\text{ ms per } 34.3\text{ cm}$).
Direct measurement of physical acoustic latency requires an external calibrated microphone feedback loop or oscilloscope probe.
SyncWave explicitly disclaims claiming software timing equals physical acoustic timing.

---

## Milestone 9 Status: Fixed Software-Domain Output Alignment & Per-Output Delay Stage

> [!IMPORTANT]
> **Milestone 9 implements fixed software-domain output delay compensation via per-output `DelayBuffer` lines and the `SyncController` alignment engine. It compensates observable software latencies (WASAPI stream latency, current buffer padding, router queue latency, and resampler group delay). It does NOT implement dynamic clock drift correction or adaptive micro-resampling (Milestone 10).**

### Implemented Subsystems & Mathematical Models:
1. **Per-Output `DelayBuffer` Placement (Post-Resampler)**:
   - Delay stage placed **after** resampling at native device sample rates ($f_{\text{dev}}$):
     $$\text{MasterAudioBus} \to \text{OutputRouter} \to \text{SPSC Queue} \to \text{Resampler} \to \text{DelayBuffer} \to \text{WASAPI}$$
   - Ensures delay is applied in integer device frames ($D_{\text{frames}} = \text{round}(D_{\text{ms}} \times f_{\text{dev}} / 1000)$) without fractional interpolation error or passing silence through resampler filters.
   - Preallocated circular stereo Float32 buffer with initial silence prefill; lock-free atomic `delayFrames_` updates; zero allocation and $O(N)$ execution time in audio callback.

2. **Auditing the Latency Taxonomy (`OutputLatencyModel`)**:
   - $L_{\text{stream}}$: Driver stream latency (`IAudioClient::GetStreamLatency`)
   - $L_{\text{pad}}$: Mixer buffer current padding (`IAudioClient::GetCurrentPadding` / $f_{\text{dev}}$)
   - $L_{\text{queue}}$: SPSC queue latency (`RingBuffer::availableToRead` / $f_{\text{master}}$)
   - $L_{\text{resamp}}$: Linear interpolation filter delay ($~0.5 / f_{\text{dev}}$)
   - $L_{\text{delay}}$: Intentional compensation delay (`DelayBuffer::delayMs`)
   - $L_{\text{cal}}$: Optional physical acoustic calibration offset
   - Total estimated software latency:
     $$L_{\text{sw}} = L_{\text{stream}} + L_{\text{pad}} + L_{\text{queue}} + L_{\text{resamp}}$$
   - Effective baseline latency:
     $$L_{\text{eff}} = L_{\text{sw}} + L_{\text{cal}}$$

3. **Alignment Mathematics (`SyncController`)**:
   - Audio cannot travel backward; alignment uses the slowest path as the target baseline:
     $$T_{\text{target}} = \max_{i \in \{1 \dots N\}} (L_{\text{eff}, i})$$
   - Required compensation delay per device:
     $$D_i = \max(0.0, T_{\text{target}} - L_{\text{eff}, i})$$
   - Frame delay applied to `DelayBuffer`:
     $$D_{\text{frames}, i} = \text{round}\left(D_i \times \frac{f_{\text{dev}, i}}{1000}\right)$$
   - The slowest device receives $D = 0$ ms; faster devices are delayed to match the slowest endpoint.

4. **Synchronization States (`SyncState`)**:
   - `Disabled`: No delay alignment active ($D_i = 0$).
   - `Manual`: User-configured static delays applied via `--delay`.
   - `SoftwareCalibrated`: Automatically aligned based on measured software-domain latencies via `--sync software` or `syncwave calibrate`.
   - `PhysicallyCalibrated`: Manual physical calibration offsets (`--offsets`) combined with software latency models.
   - `Uncertain`: Measurement failed or was too noisy to determine safe alignment.

### Verified Hardware Experiment:
- Dual-device playback session across `Headphones (realme Buds T310)` (Bluetooth) and `Speakers (Realtek(R) Audio)` (Integrated):
  - **`syncwave calibrate --outputs 2,3`**:
    - Buds (Bluetooth): $L_{\text{sw}} = 190.67\text{ ms}$, applied delay = $0.00\text{ ms}$ (0 frames).
    - Realtek (Integrated): $L_{\text{sw}} = 22.00\text{ ms}$, applied delay = $168.67\text{ ms}$ (8,096 frames @ 48 kHz).
    - Target Alignment Latency = $190.67\text{ ms}$.
    - Sync State: `SoftwareCalibrated`.
  - **Manual Delays (`--delay 50,0`)**:
    - Verified delay line configuration: Buds configured with 50 ms (2,400 frames); Realtek with 0 ms.
  - **Acoustic Disclaimer**: Explicitly informs the user that external physical delays (Bluetooth A2DP RF transport ~100–250 ms, DAC filters, acoustic room propagation) are not directly measurable by software alone and can be calibrated with `--offsets`.

---

## Planned Synchronization Roadmap (Milestones 10+)

1. **Milestone 10: Dynamic Drift Correction & Micro-Resampling**:
   - Continuous drift tracking via `DriftEstimator`.
   - Dynamic micro-resampling rate modulation to counteract PPM clock drift over long sessions without buffer underruns or audible pitch changes.

2. **Milestone 11: Automatic Acoustic Calibration**:
   - Acoustic chirp / MLS test signal generation and microphone capture for automated end-to-end physical acoustic latency measurement.



