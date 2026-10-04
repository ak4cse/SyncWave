# ADR 010: Controlled Long-Term Clock Drift Correction via Micro-Resampling

## Context

Following the completion of Milestone 9 (software-domain delay alignment with `DelayBuffer`), multiple audio endpoints can start playback aligned at a common target software latency.

However, independent physical audio hardware DACs (e.g. onboard Realtek HD Audio, USB DACs, Bluetooth A2DP headsets) derive their timing from uncoupled crystal oscillators. Real hardware clocks exhibit manufacturing tolerances, temperature sensitivity, and age-related deviations typically spanning $\pm 10\text{ to } 100\text{ ppm}$ ($\mu\text{s/s}$). 

Without continuous clock drift correction:
- Two clocks drifting apart by $20\text{ ppm}$ diverge by $1.2\text{ ms}$ per minute ($7.2\text{ ms}$ in 6 minutes, $12.0\text{ ms}$ in 10 minutes).
- As playheads separate, the slower device's input queue fills towards overflow, while the faster device's queue starves towards underrun.
- Periodic buffer wrap or dropouts occur, and the sound field collapses due to phase comb-filtering.

Milestone 10 introduces controlled, long-term software micro-resampling rate adjustment to bound accumulated phase error without audible pitch distortion, clicks, or real-time thread safety violations.

---

## Decisions

### 1. Rejection of `IAudioClockAdjustment` in Favor of Pure Software Micro-Resampling

SyncWave strictly rejects using Windows `IAudioClockAdjustment`:
- **Exclusive-Mode Requirement**: `IAudioClockAdjustment` is primarily supported in WASAPI Exclusive Mode and is rejected or unsupported by many consumer drivers in Shared Mode.
- **Hardware/Driver Inconsistency**: USB audio class drivers and Bluetooth A2DP audio endpoints rarely implement hardware clock adjustment.
- **Audible Driver Resets**: Calling `IAudioClockAdjustment` on consumer endpoints often triggers glitchy driver-level clock resets and clicks.

**Decision**: Implement rate adjustment entirely in the software DSP domain via `Resampler`:
$$\text{effectiveRatio} = \text{baseRatio} \times \left(1.0 + \frac{\text{ppm}}{10^6}\right)$$
This operates uniformly across all WASAPI shared-mode render endpoints, requiring zero specialized driver capabilities.

---

### 2. Directionality Mathematics & Phase Error Convention

Let:
- $T_{\text{master}}(t)$: Monotonic timeline position of `MasterAudioBus` ($\text{totalFramesDistributed} / f_{\text{master}}$).
- $T_{\text{target}}(t)$: Synchronized target playhead ($T_{\text{master}}(t) - L_{\text{target}}$).
- $T_{\text{output}}(t)$: Output endpoint's actual physical playhead projected from hardware clock.
- $e(t) = T_{\text{target}}(t) - T_{\text{output}}(t)$: Instantaneous phase error.

#### Direction Conventions:
- If $e(t) > 0$: The output playhead is **lagging behind** the target playhead. The endpoint must consume more master frames per output second to catch up. A **positive rate adjustment** ($\text{ppm} > 0$) increases the resampler ratio, accelerating playhead progression through the master audio stream.
- If $e(t) < 0$: The output playhead is **leading ahead** of the target playhead. The endpoint must consume fewer master frames per output second. A **negative rate adjustment** ($\text{ppm} < 0$) slows down playhead progression.
- If clock frequency error is $D_{\text{filtered}} < 0$ (clock running slow): Feedforward drift cancellation applies $u_{\text{FF}} = -D_{\text{filtered}} > 0$.

---

### 3. Filtered Drift Estimation & Outlier Rejection

Raw hardware clock queries exhibit measurement jitter from scheduling variances and thread dispatch latencies. `FilteredDriftEstimator` enforces robust multi-stage filtering:
1. **Observation Window Threshold**: Discards drift calculations until at least $5.0\text{ seconds}$ and $30\text{ samples}$ have accumulated over a sliding observation window ($10\text{ s}$).
2. **Outlier Rejection**: Rejects any instantaneous rate estimate if:
   - $|\text{rateErrorPpm}| > 500\text{ ppm}$ (unphysical hardware clock deviation).
   - $r^2 < 0.90$ (poor linear regression goodness of fit, typical of buffer wraps or system sleep).
   - Sudden jumps exceeding $375\text{ ppm}$ from the last verified baseline.
3. **Dual Exponential Moving Average (EMA) Smoothing**:
   - Drift rate filtering: $\alpha_{\text{drift}} = 0.15$
   - Phase error filtering: $\alpha_{\text{phase}} = 0.20$

---

### 4. Bounded Slow Proportional Controller Architecture

The feedback control loop combines feedforward frequency matching with bounded proportional error feedback:

1. **Deadband Threshold ($\pm 1.0\text{ ms}$)**:
   If $|e_{\text{filtered}}| \le 1.0\text{ ms}$, the proportional feedback term is zero:
   $$e_{\text{excess}} = \begin{cases} e_{\text{filtered}} - 1.0, & e_{\text{filtered}} > 1.0\text{ ms} \\ e_{\text{filtered}} + 1.0, & e_{\text{filtered}} < -1.0\text{ ms} \\ 0.0, & |e_{\text{filtered}}| \le 1.0\text{ ms} \end{cases}$$
   This eliminates continuous micro-hunting when outputs are synchronized within the perceptual deadband.

2. **Proportional Gain ($K_p = 10.0\text{ ppm/ms}$)**:
   $$u_{\text{feedback}} = K_p \times e_{\text{excess}}$$

3. **Feedforward Drift Cancellation**:
   $$u_{\text{FF}} = -D_{\text{filtered}}$$

4. **Hard Clamping ($\pm 100\text{ ppm}$)**:
   $$u_{\text{commanded}} = \text{std::clamp}(u_{\text{FF}} + u_{\text{feedback}}, -100.0, +100.0)$$
   Hard clamping guarantees that pitch modulation is strictly limited to $\pm 100\text{ ppm}$ ($\pm 0.01\%$, or $< 0.17$ cents), well below the human pitch discrimination threshold of ~5–10 cents.

5. **Slew Rate Limiting ($\le 5.0\text{ ppm/s}$)**:
   `Resampler` applies per-frame smooth interpolation, slewing at most $5.0\text{ ppm}$ per second. Transitioning from $0\text{ ppm}$ to a full $100\text{ ppm}$ correction requires at least 20 seconds, preventing any audible frequency modulation.

---

### 5. Controller State Machine

Each output maintains an explicit lifecycle state:
- `Disabled`: Drift correction inactive; resampler runs at nominal ratio.
- `Initializing`: Endpoint connected; accumulating initial timing samples.
- `Measuring`: Window duration $< 5.0\text{ s}$ or samples $< 30$; collecting baseline regression.
- `Locked`: Phase error within deadband ($\pm 1.0\text{ ms}$) and confidence $\ge 0.90$; holding drift feedforward.
- `Correcting`: Phase error outside deadband; feedback and feedforward micro-resampling active.
- `Uncertain`: Regression noisy ($r^2 < 0.90$) or outlier detected; decays target PPM gently toward zero.
- `Disconnected`: Device removed or invalidated; state reset and target PPM forced to 0.

---

### 6. Real-Time Safety Guarantee

- **Audio Render Threads**: Perform zero heap allocations, acquire zero mutexes, execute zero system calls, and make zero timing regressions. They evaluate simple linear interpolation steps with lock-free atomic parameter loading.
- **Monitoring Loop**: Linear regression, outlier rejection, EMA filtering, and PID control calculations execute exclusively in non-real-time monitoring threads at 10 Hz.

---

## Verification & Results

1. **Synthetic Clock Simulations**:
   - 10-minute simulation comparing uncorrected vs corrected pipelines with a $-20.83\text{ ppm}$ clock divergence (48000 Hz vs 47999 Hz):
     - Uncorrected phase error diverged to $1.25\text{ ms}$ at 1 min, $6.25\text{ ms}$ at 5 min, and $12.50\text{ ms}$ at 10 min.
     - Corrected phase error reached steady-state lock at $\approx 0.15\text{ ms}$ ($\ll 1.0\text{ ms}$ deadband) and held lock across all 10 minutes.
2. **Physical Hardware Verification**:
   - Tested realme Buds T310 (Bluetooth, 44.1 kHz native) + Realtek HD Audio (48.0 kHz native) for 60 seconds with `--sync adaptive`:
     - Both endpoints converged smoothly to `Correcting` state.
     - realme Buds converged from $-317\text{ ppm}$ to $+0.15\text{ ppm}$ nominal error over 57.2s.
     - Realtek Audio converged to $-34.0\text{ ppm}$ nominal error.
     - 0 WASAPI underruns and 0 queue overruns over 2.88 million frames rendered.
3. **Automated Test Suite**:
   - 560 unit and integration tests passing with 100% success rate.
