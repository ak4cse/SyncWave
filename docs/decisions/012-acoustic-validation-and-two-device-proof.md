# ADR 012: Acoustic Arrival Calibration Validation & Two-Device Proof

## Context & Scientific Qualification

In Milestone 11, SyncWave introduced acoustic calibration via swept-frequency chirp emission and microphone capture. Milestone 11.1 resolves critical methodological questions identified in the review:

1. **Clarifying Measurement Semantics**:
   - The measured quantity $L_{\text{arrival}}$ is **not** the intrinsic latency of speaker hardware alone.
   - It is the **end-to-end acoustic arrival offset**:
     $$L_{\text{arrival}} = L_{\text{output\_sw}} + L_{\text{DAC/DSP}} + L_{\text{driver}} + L_{\text{air}} + L_{\text{mic\_filter}} + L_{\text{mic\_buffering}}$$
   - When measuring two output devices using the identical microphone and fixed room geometry:
     $$\Delta L = L_{\text{arrival}, A} - L_{\text{arrival}, B} = (L_{\text{out}, A} + L_{\text{mic}}) - (L_{\text{out}, B} + L_{\text{mic}}) = L_{\text{out}, A} - L_{\text{out}, B}$$
     The common microphone pipeline latency cancels out completely.

2. **Audit of M11 "Repeatability" & Multi-Peak Phenomena**:
   - In preliminary M11 runs, 2 out of 5 runs passed, with an apparent secondary detection at 139.98 ms vs ~107.40 ms.
   - Investigation reveals:
     - Secondary peaks occur at $\approx +32\text{ to } +56\text{ ms}$ separation due to acoustic room flutter/reflections or Windows audio enhancement processing windows.
     - Single-run detections with high noise or clipping can jump to secondary peaks if primary correlation drops.
     - Stricter multi-peak telemetry is required to inspect primary score, secondary peak score, peak-to-secondary ratio, and peak separation.

---

## Decisions

### 1. Multi-Peak Telemetry in `CorrelationDetector`
The normalized cross-correlation detector was augmented to identify the strongest secondary peak outside the primary exclusion radius ($\pm 480\text{ frames}$ / $10\text{ ms}$):
- `secondaryPeakScore`: Amplitude of secondary correlation peak.
- `secondaryPeakIndex` / `secondaryArrivalTimeSec`: Timing of the secondary reflection.
- `peakSeparationMs`: Delay between primary and secondary peaks ($\Delta t_{\text{ref}} = t_{\text{sec}} - t_{\text{prim}}$).
- `peakToSecondaryRatio`: Ratio $r = \frac{\gamma_{\text{primary}}}{\max(\gamma_{\text{secondary}}, 10^{-4})}$. A high ratio ($> 2.5\times$) proves unambiguous direct-path dominance over multipath room flutter.

### 2. Rigorous Session Acceptance Criteria
To ensure true statistical repeatability:
- Default requested sweeps increased to $N = 7$.
- Minimum valid runs strictly enforced at $K \ge 5$ (sessions with $< 5$ valid runs are marked `REJECTED`).
- Outlier filtering uses Median Absolute Deviation (MAD): $|L_i - \text{median}| \le \max(3 \times \text{MAD}, 15.0\text{ ms})$.
- Max acceptable standard deviation $\sigma \le 15.0\text{ ms}$.
- If validation fails, `CalibrationStore` **never** overwrites previous records; the failed result is reported with explicit rejection rationale.

### 3. Verification of Calibration Direction
The latency alignment plan applies delay inversely proportional to device arrival offset:
$$D_i = \max_k(L_{\text{effective}, k}) - L_{\text{effective}, i}$$
where $L_{\text{effective}} = L_{\text{software}} + L_{\text{arrival}}$.
- Faster acoustic device receives **more** delay line buffering ($D_{\text{fast}} > 0$).
- Slower acoustic device receives **zero** delay ($D_{\text{slow}} = 0$).

---

## Validation & Experimental Evidence

### 1. Automated Test Suite
- Test count expanded from 1,086 to **1,101 passing assertions** (0 failed).
- Added explicit unit tests for:
  - Multi-run statistical rejection when valid runs $< 5$.
  - Detection rejection when variance exceeds limits.
  - Multi-peak detection and secondary reflection separation.
  - Two-device physical calibration plan direction.

### 2. Live Hardware Verification (Windows 11 Host)
- **Output 1 (Realtek Speakers)**:
  - 5 of 7 runs valid (runs 1–5: 161.47, 161.37, 161.68, 162.73, 160.90 ms).
  - Median arrival: **161.47 ms**, uncertainty $\sigma = \pm 0.68\text{ ms}$.
  - Primary-to-secondary peak ratio: $2.0\times\text{ to } 4.7\times$.
- **Output 2 (realme Buds T310 Bluetooth A2DP)**:
  - 5 of 7 runs valid (median arrival: **131.68 ms**, uncertainty $\sigma = \pm 7.14\text{ ms}$).
- **Two-Device Delay Plan**:
  - `Speakers (Realtek)`: receives **148.88 ms** delay line buffering (7,146 frames).
  - `Headphones (realme Buds)`: receives **0.00 ms** delay line buffering (0 frames).
  - Target synchronized timeline: **422.34 ms**.
  - State: `PhysicallyCalibrated`.
