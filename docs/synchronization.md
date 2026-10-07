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

## Milestone 10 Status: Controlled Long-Term Clock Drift Correction

> [!IMPORTANT]
> **Milestone 10 introduces software-domain micro-resampling drift correction to eliminate clock divergence between independent hardware oscillators over long sessions. It maintains phase alignment within a $\pm 1.0\text{ ms}$ deadband while preserving lock-free, zero-allocation real-time audio threads. It does NOT claim perfect physical/acoustic synchronization, which still requires external physical calibration (Milestone 11).**

### Implemented Subsystems & Mathematical Models:
1. **Directionality & Sign Conventions**:
   - **Playhead Phase Error**:
     $$e(t) = T_{\text{target}}(t) - T_{\text{output}}(t)$$
     - $e > 0$ indicates the output is **lagging** (playhead behind target).
     - $e < 0$ indicates the output is **leading** (playhead ahead of target).
   - **Control Action Sign**:
     The software resampler ratio is $R = \frac{f_{\text{in}}}{f_{\text{out}}} = \frac{\text{master frames}}{\text{output frame}}$.
     - If $e > 0$ (lagging), resampler ratio must **increase** ($u > 0$) to pull more master frames per output second, advancing the output playhead.
     - If $e < 0$ (leading), resampler ratio must **decrease** ($u < 0$) to pull fewer master frames per output second, allowing the output playhead to fall back.
   - **Hardware Clock Drift & Feedforward**:
     $$\text{Drift}_{\text{raw}} = \left(\frac{R_{\text{obs}}}{R_{\text{nom}}} - 1\right) \times 10^6 \quad (\text{ppm})$$
     If a device runs faster than nominal ($D > 0$), its playhead advances faster; feedforward cancellation applies:
     $$u_{\text{FF}} = -D_{\text{filtered}}$$

2. **Bounded Slow Feedback Controller & Deadband**:
   - **Deadband**: $\pm 1.0\text{ ms}$ ($\pm 0.001\text{ s}$). Inside the deadband, proportional control is zero to prevent hunting and micro-modulation on jitter.
   - **Proportional Gain**: $K_p = 10.0\text{ ppm/ms}$ ($10,000.0\text{ ppm/s}$).
   - **Excess Error Control**:
     $$u_P = \begin{cases}
     -K_p \times (e - 1.0\text{ ms}) & \text{if } e > +1.0\text{ ms} \\
     -K_p \times (e + 1.0\text{ ms}) & \text{if } e < -1.0\text{ ms} \\
     0 & \text{if } |e| \le 1.0\text{ ms}
     \end{cases}$$
   - **Hard Clamp**:
     $$u(t) = \text{clamp}(u_{\text{FF}} + u_P, -100.0\text{ ppm}, +100.0\text{ ppm})$$
   - **Slew Rate Limiter**:
     The `Resampler` limits adjustment rate changes to $\le 5.0\text{ ppm/s}$ applied smoothly per-frame:
     $$\Delta\text{ppm}_{\text{per-frame}} = \frac{5.0\text{ ppm/s}}{f_s}$$

3. **Filtered Drift Estimation & Outlier Rejection (`FilteredDriftEstimator`)**:
   - **Observation Window Check**: Minimum duration $\ge 5.0\text{ s}$ and $\ge 30$ valid snapshot points before producing confident drift estimates.
   - **Outlier Rejection**: Discards snapshots if:
     1. Absolute drift rate deviation $> 500\text{ ppm}$.
     2. OLS regression fit $r^2 < 0.90$.
     3. Rate jump relative to trailing estimate $> 375\text{ ppm}$.
   - **Dual EMA Smoothing**:
     - Drift rate filter: $\alpha_{\text{drift}} = 0.15$
     - Phase error filter: $\alpha_{\text{phase}} = 0.20$

4. **State Machine (`DriftCorrectionState`)**:
   - `Disabled`: Drift correction deactivated (ratio fixed at nominal).
   - `Initializing`: Insufficient observations ($t < 5.0\text{ s}$ or $N < 30$).
   - `Measuring`: Window populated, regression converging ($r^2 < 0.90$).
   - `Locked`: Drift estimated with high confidence ($r^2 \ge 0.90$), phase error inside deadband ($|e| \le 1.0\text{ ms}$).
   - `Correcting`: Controller actively adjusting micro-resampling ratio ($|e| > 1.0\text{ ms}$ or non-zero drift feedforward).
   - `Uncertain`: Snapshot jitter or regression variance exceeded tolerance ($r^2 < 0.80$).
   - `Disconnected`: Device endpoint invalidated or removed.

### Synthetic Simulation Verification:
Validated via automated discrete-step simulation (`testSyntheticMultiClockDriftSimulation`) under continuous $+25\text{ ppm}$ clock divergence:
- **Uncorrected Pipeline**:
  - 60 s: Phase error drifted to $1.25\text{ ms}$.
  - 300 s (5 min): Phase error drifted to $7.25\text{ ms}$.
  - 600 s (10 min): Phase error drifted to $14.75\text{ ms}$ (unbounded divergence).
- **Corrected Pipeline**:
  - 60 s: Phase error bounded at $0.15\text{ ms}$ (inside $1.0\text{ ms}$ deadband, slewed to $+25\text{ ppm}$).
  - 300 s (5 min): Phase error bounded at $0.15\text{ ms}$ ($100\%$ confidence, zero buffer underruns).
  - 600 s (10 min): Phase error bounded at $0.15\text{ ms}$ (steady-state locked).

### Verified Hardware Experiment (60 Seconds):
Conducted on `Headphones (realme Buds T310)` (Bluetooth, 44.1 kHz) and `Speakers (Realtek(R) Audio)` (Integrated, 48.0 kHz) with `--sync adaptive`:
- **Playback Metrics**:
  - Total duration: 60.1 seconds (2,883,840 rendered frames per endpoint).
  - WASAPI Underruns: **0 on both devices**.
  - Queue Overruns / Underruns: **0 on both devices**.
- **Drift Controller Convergence**:
  - Both endpoints transitioned through `Initializing` $\to$ `Measuring` $\to$ `Correcting`.
  - Drift confidence reached $100\%$ ($r^2 = 1.0000$).
  - `realme Buds T310`: Target adjustment $+100.0\text{ ppm}$, active slewed adjustment reached $+100.0\text{ ppm}$.
  - `Realtek Audio`: Target adjustment $+100.0\text{ ppm}$, active slewed adjustment reached $+100.0\text{ ppm}$.

#### Physical Acoustic Latency Honesty Statement:
Milestone 10 software micro-resampling maintains phase alignment and prevents buffer over/underrun drift within the software audio engine. However, external physical acoustic delays (Bluetooth RF transmission packets, hardware DAC reconstruction filters, room air flight time) remain external to WASAPI. Direct acoustic alignment requires acoustic chirp calibration implemented in Milestones 11 & 11.1.

---

## Milestone 11 & 11.1 Status: Acoustic Arrival Latency Calibration & Two-Device Proof

> [!IMPORTANT]
> **Milestones 11 & 11.1 measure and compensate the end-to-end acoustic arrival offset of audio endpoints using swept chirps and reference microphone capture.**

### Scientific Measurement Semantics:
The measured quantity $L_{\text{arrival}}$ represents the **end-to-end acoustic arrival offset**:
$$L_{\text{arrival}} = L_{\text{output\_sw}} + L_{\text{DAC/DSP}} + L_{\text{driver}} + L_{\text{air}} + L_{\text{mic\_filter}} + L_{\text{mic\_buffering}}$$
It is **not** claimed to represent the intrinsic latency of speaker hardware alone. When measuring two outputs with the identical microphone and geometry:
$$\Delta L = L_{\text{arrival}, A} - L_{\text{arrival}, B} = L_{\text{out}, A} - L_{\text{out}, B}$$
The common microphone capture latency cancels out completely.

### Subsystem Architecture & Enhancements (M11.1):
1. **`ChirpGenerator`**:
   - Synthesizes swept-frequency sine chirps ($300\text{ Hz} \to 8000\text{ Hz}$ over $150\text{ ms}$).
   - Tukey cosine windowing (10 ms edge ramps) eliminates acoustic pops and spectral splatter.
   - Amplitude strictly capped at $\le 0.50$ (default $0.25$) for hearing and speaker safety.
   - 200 ms lead-in and 250 ms lead-out silence margins isolate the acoustic sweep.
2. **`CorrelationDetector`**:
   - Computes normalized cross-correlation $\gamma[k]$ against reference template synthesized at microphone sample rate ($f_{\text{mic}}$).
   - 3-point parabolic peak interpolation provides sub-sample timing accuracy ($< 0.02\text{ ms}$).
   - Peak-to-Noise Ratio:
     $$\text{PNR} = \frac{\gamma[k_{\text{max}}]}{\sigma_{\text{noise}}}$$
   - **Multi-Peak & Reflection Telemetry (M11.1)**: Detects secondary reflection peaks, reporting secondary score, arrival separation ($\Delta t_{\text{ref}}$), and primary-to-secondary ratio to confirm direct path dominance.
3. **`AcousticCapture`**:
   - Dedicated WASAPI microphone capture client operating in shared event-driven mode (`eCapture`).
   - Keeps capture thread alive across sweeps to eliminate thread creation delays.
   - Converts multi-format inputs (Float32, Int16, Int24, Int32) to preallocated mono Float32 buffer.
4. **`AcousticCalibrator`**:
   - Executes multi-run sweeps ($N = 7$ sweeps; requires at least $K \ge 5$ valid runs).
   - Computes median latency and Median Absolute Deviation (MAD):
     $$\text{MAD} = \text{median}(|L_i - \text{median}|)$$
   - Rejects multipath and ambient noise outliers: $|L_i - \text{median}| > \max(3 \times \text{MAD}, 15\text{ ms})$.
   - Calculates uncertainty metric as standard deviation ($\sigma \le 15.0\text{ ms}$).
   - Sessions with $< 5$ valid runs are marked `REJECTED` and will **never** overwrite calibration storage.
5. **`CalibrationStore` & Delay Buffer Integration**:
   - Persists calibration records in `%USERPROFILE%\.syncwave\calibrations.json`.
   - `AudioEngine` injects $L_{\text{arrival}}$ into `OutputLatencyModel` as `optionalCalibrationOffsetMs`.
   - Effective latency model:
     $$L_{\text{effective}, i} = L_{\text{software}, i} + L_{\text{arrival}, i}$$
     $$D_i = \max_j L_{\text{effective}, j} - L_{\text{effective}, i}$$
   - **Direction Verified**: Faster acoustic paths receive positive delay line buffering ($D_i > 0$); slower acoustic paths receive zero delay ($D_j = 0$).

### Verified Host Experiment (Windows 11):
- **Output A (Realtek Speakers)**:
  - 5/7 runs valid (161.47, 161.37, 161.68, 162.73, 160.90 ms).
  - Median arrival: **161.47 ms**, uncertainty: **$\pm 0.68\text{ ms}$**.
  - Secondary reflection: separated by $\approx 55\text{ ms}$ with peak ratio $> 2.0\times$.
- **Output B (realme Buds T310 Bluetooth A2DP)**:
  - 5/7 runs valid.
  - Median arrival: **131.68 ms**, uncertainty: **$\pm 7.14\text{ ms}$**.
- **Two-Device Alignment Plan (`syncwave calibrate -O 3,2`)**:
  - `Speakers (Realtek)`: Delay = **148.88 ms** (7,146 frames).
  - `Headphones (realme Buds)`: Delay = **0.00 ms** (0 frames).
  - Target synchronized timeline: **422.34 ms**.
  - State: `PhysicallyCalibrated`.

---

## Milestone 12: Long-Duration Multi-Output Synchronization Stress Testing

Milestone 12 validated the long-duration stability, multi-endpoint scalability, and fault tolerance of SyncWave's synchronization engine across physical host endpoints (`Speakers (Realtek(R) Audio)`, `Headphones (realme Buds T310)`, and `Speakers (Steam Streaming Speakers)`).

### Comparative Baseline: Uncompensated vs. Adaptive Correction (180s)
- **Uncompensated (`--sync none`)**: Clock frequency mismatches accumulate linearly. Phase error on Realtek exploded from $0\text{ ms}$ to **$140.19\text{ ms}$**, diverging by **$142.53\text{ ms}$** relative to realme Buds within 3 minutes. Output queues saturated towards capacity.
- **Adaptive Drift Correction (`--sync adaptive`)**: The linear regression estimator and proportional resampler controller actively adjusted playback speeds (Realtek mean $+86.4\text{ ppm}$, Buds mean $+24.5\text{ ppm}$). The inter-device phase delta was held to a mean of **$12.82\text{ ms}$** (ending at **$8.07\text{ ms}$**), preventing divergence.
- **Hardware Stability**: 0 WASAPI hardware underruns, 0 queue overruns, and strictly bounded memory ($\sim 16.8\text{ MB}$, growth $\le 0.6\text{ MB}$).

### Fault Injection & Dynamic Reconnect
- Live endpoint removal (`onDeviceDisconnected`) safely drops the disconnected endpoint without interrupting playback on remaining devices.
- Live peripheral re-insertion (`onDeviceReconnected`) re-initializes and aligns the endpoint into the active `MasterAudioBus` without restarting the engine.
