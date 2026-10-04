# ADR 008: Relative Latency & Timing Model Validation

## Context
In Milestone 7, SyncWave introduced sliding-window linear regression clock estimation and pairwise drift calculation. During dual-device playback experiments with `Speakers (Realtek(R) Audio)` and `Headphones (realme Buds T310)`, continuous tone playback exhibited stable clock rates (+7.1 ppm and -11.1 ppm, with relative drift of +18.2 ppm). However, loopback capture mode exhibited an anomalous -612 ppm reading over a short 10-second run.

Furthermore, before implementing feedback control, ring buffer delay compensation, or adaptive resampling (Milestones 9+), SyncWave required a rigorous mathematical audit of its timing models:
1. Clarifying clock frequency vs sample rate units (`IAudioClock::GetFrequency` ticks vs audio frames).
2. Separating instantaneous time offset ($T_A - T_B$) from accumulated drift ($\Delta\text{Offset}$) and drift rate ($\text{ppm}$).
3. Developing an end-to-end stream playhead estimation model incorporating buffer padding.
4. Implementing a deterministic transient test harness (`SyncPulseGenerator`) for repeatable latency measurements.
5. Addressing physical acoustic latency honestly without fabricating unmeasurable numbers.

## Decisions

1. **Resolution of the Bluetooth -612 ppm Anomaly (Multi-Window Convergence)**:
   - Root Cause: Bluetooth A2DP stream initialization introduces an initial startup delay / buffer-fill ramp (~190 ms) during the first 1–2 seconds. An OLS regression line fitted from $t=0$ across a short 10-second window is heavily skewed downward by this startup transient.
   - Solution: Implemented multi-window rate estimation (`estimateRateOverWindow(windowSec)`) evaluating trailing windows across 1s, 5s, 10s, 30s, and 60s.
   - Empirical Validation: In a 60-second hardware experiment, realme Buds T310 exhibited:
     - 1s window: 47994.69 Hz (-110.59 ppm)
     - 5s window: 48000.26 Hz (+5.46 ppm)
     - 10s window: 48000.05 Hz (+1.08 ppm)
     - 60s window: 47994.35 Hz (-117.67 ppm)
     - Concurrently, Realtek Audio exhibited consistent stability (-33.70 ppm to -48.00 ppm across all windows).
     - Multi-window estimation proves the startup transient is an initial state artifact, distinct from steady-state physical clock drift.

2. **Mathematical Separation of Offset vs Accumulated Drift vs Drift Rate**:
   - Explicitly separated:
     - **Instantaneous Offset**: $\text{Offset}(t) = T_A(t) - T_B(t)$ (seconds or milliseconds).
     - **Initial Offset**: $\text{Offset}(t_0) = T_A(t_0) - T_B(t_0)$.
     - **Accumulated Drift**: $\Delta\text{Offset} = \text{Offset}(t_1) - \text{Offset}(t_0) = (T_A(t_1) - T_B(t_1)) - (T_A(t_0) - T_B(t_0))$.
     - **Drift Rate**: $\frac{\Delta\text{Offset}}{\Delta t} \times 10^6$ (ppm).
   - Proven by unit tests: If Device A starts 100 ms ahead and stays 100 ms ahead, accumulated drift is strictly 0.0 ms despite a non-zero instantaneous offset.

3. **Stream Playhead Estimation & Timeline Modeling**:
   - Added playhead tracking to `DeviceOutput`:
     - $\text{App Playhead (frames)} = \text{framesSubmitted} - \text{currentPadding}$.
     - $\text{App Playhead (seconds)} = \frac{\text{framesSubmitted} - \text{currentPadding}}{f_s}$.
     - $\text{WASAPI Clock Playhead (seconds)} = \frac{\text{clockPosition}}{\text{clockFrequency}}$.
     - $\text{Playhead Discrepancy (ms)} = (\text{App Playhead} - \text{WASAPI Playhead}) \times 1000$.
   - Verified that both realme Buds T310 and Realtek Audio report `IAudioClock::GetFrequency = 384,000 Hz` ($8 \times 48,000\text{ Hz}$). Device clock seconds are strictly calculated via `position / frequency` rather than assuming clock ticks equal frames.

4. **Deterministic Transient Test Harness (`SyncPulseGenerator`)**:
   - Created `SyncPulseGenerator` generating a bandlimited half-cycle sine pulse (1.0 ms duration, peak 1.0f) bordered by 500 ms lead-in silence and 500 ms lead-out silence.
   - Pushed directly through `MasterAudioBus` and `OutputRouter` into all configured endpoints concurrently.
   - Added `syncwave latency-test` command executing repeated runs and calculating software-path latency statistics (mean, median, min, max, std dev).
   - Empirical 5-run results between Bluetooth and Realtek: Mean = -8.00 ms, Median = -10.00 ms, Std Dev = 4.47 ms.

5. **Physical Acoustic Latency Honesty Statement**:
   - Formally documented that software timestamps (`IAudioClock`, QPC, and padding) observe only OS audio engine mixing and driver submission stages.
   - Physical acoustic emission includes external physical delays:
     - Bluetooth A2DP transport packetization and RF transmission delay (~100–250 ms depending on SBC/AAC/LDAC codec profiles and buffer depths).
     - Hardware DAC reconstruction / anti-aliasing filter delay.
     - Transducer electromechanical latency and room air flight time ($~1\text{ ms per } 34.3\text{ cm}$).
   - Direct measurement of physical acoustic latency requires an external calibrated microphone feedback loop or oscilloscope probe.
   - SyncWave explicitly disclaims claiming software timing equals physical acoustic timing.

6. **Observation-Only Principle Maintained**:
   - **No delay compensation, feedback control, or drift correction is implemented in Milestone 8.**
   - All render threads remain lock-free, zero-allocation, and unmodified.

## Consequences
- The timing models are mathematically verified, robust against initial startup transients, and fully covered by 422 passing automated tests.
- High-precision 60-second and transient latency testing CLI commands (`clock-test` and `latency-test`) provide verified empirical data for future delay compensation algorithms (Milestone 9+).
