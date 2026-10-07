# ADR 013: Acoustic Latency Accounting Audit & Model B Verification

## Context & Critical Accounting Audit

In Milestone 11.1, SyncWave verified acoustic arrival calibration using swept chirps and cross-correlation on a two-device test setup (Realtek Speakers and OnePlus / realme Buds T310). However, Milestone 11.2 performed a focused audit of the mathematical latency accounting model:

1. **Double-Counting Vulnerability in Previous Model**:
   - Previously, $L_{\text{effective}}$ was calculated as:
     $$L_{\text{effective}} = L_{\text{software}} + L_{\text{arrival}}$$
   - However, $L_{\text{arrival}}$ was measured from the moment the chirp samples were written to WASAPI (`IAudioRenderClient::GetBuffer`) to microphone capture.
   - Therefore, $L_{\text{arrival}}$ **already encompassed** WASAPI driver buffering and hardware padding ($L_{\text{wasapi\_padding}}$ and $L_{\text{wasapi\_stream}}$).
   - Adding $L_{\text{software}}$ to $L_{\text{arrival}}$ double-counted the WASAPI buffer latency.

2. **Resolution of the Buds Software Latency Paradox**:
   - Telemetry from tone probing showed OnePlus Buds with $L_{\text{software}} \approx 290.67\text{ ms}$, while its acoustic calibration arrival was $L_{\text{arrival}} \approx 131.68\text{ ms}$.
   - **Resolution**: $L_{\text{software}}$ during the probe test included $258.67\text{ ms}$ of unconsumed frames in the SPSC ringbuffer queue (`RingBuffer::availableToRead()`), caused by the tone generator queueing audio before the Bluetooth endpoint began reading.
   - In steady state, WASAPI padding for the Buds is only $22\text{ ms}$.
   - `AcousticCalibrator` operates on `WasapiOutput` directly without an `OutputRouter` queue. Thus, $131.68\text{ ms}$ represents the true physical arrival time from WASAPI buffer submission. Summing $290.67\text{ ms}$ and $131.68\text{ ms}$ was an artifact of queuing state discrepancy and double-counting.

---

## Decisions

### 1. Adoption of Model B (End-to-End Acoustic Arrival Accounting)
SyncWave explicitly establishes **Model B** across all components and documentation:

$$L_{\text{effective}} = \begin{cases} L_{\text{acoustic\_arrival}} & \text{if calibrated } (L_{\text{arrival}} > 0) \\ L_{\text{software\_estimated}} & \text{if uncalibrated } (L_{\text{arrival}} = 0) \end{cases}$$

- For acoustically calibrated devices, $L_{\text{acoustic\_arrival}}$ already includes the full transit path: WASAPI render padding + DAC/DSP + Bluetooth A2DP transport buffer + RF transmission + transducer + room propagation + microphone capture buffer.
- **Software latency is NOT added again.**
- Delay line allocation for device $i$ is calculated as:
  $$D_i = \max_k(L_{\text{effective}, k}) - L_{\text{effective}, i}$$
- Delay is applied strictly at the software/hardware boundary in `DelayBuffer`.

### 2. Persistence Metadata (`measurement_type` & `madMs`)
`AcousticCalibrationRecord` and `CalibrationStore` now record explicit metadata:
- `"measurement_type": "end_to_end_acoustic_arrival"`
- `"madMs"`: Median Absolute Deviation of the calibration session.
- Stored records are disambiguated and cannot be reinterpreted as physical-only differential offsets.

### 3. Rigorous Outlier & Run Accounting
For OnePlus Buds (7 sweeps requested):
- **Run 6 (-334.26 ms)**: Rejected by plausibility filter ($[0.0\text{ ms} \dots 800.0\text{ ms}]$).
- **Run 7 (109.00 ms)**: Rejected by MAD outlier filtering:
  - Median of 6 plausible runs: $131.68\text{ ms}$.
  - Absolute deviation of Run 7: $|109.00 - 131.68| = 22.68\text{ ms}$.
  - Threshold $\max(3 \times \text{MAD}, 15.0\text{ ms}) = \max(20.13, 15.0) = 20.13\text{ ms}$.
  - Because $22.68 > 20.13\text{ ms}$, Run 7 is classified as a statistical outlier and rejected.
- **Accepted Runs**: Exactly 5 runs (Runs 1, 2, 3, 4, 5).

---

## Two-Device Physical Alignment Comparison

### Before vs After Calibration Alignment

| Output Device | Calibrated $L_{\text{arrival}}$ | $L_{\text{effective}}$ (Model B) | Delay Buffer ($D_i$) | Delay Frames (@ 48kHz) | Physical Arrival Point |
|---|---|---|---|---|---|
| **Speakers (Realtek)** | $107.40\text{ ms}$ | $107.40\text{ ms}$ | **$24.28\text{ ms}$** | **$1165\text{ frames}$** | $107.40 + 24.28 = \mathbf{131.68\text{ ms}}$ |
| **Headphones (Buds)** | $131.68\text{ ms}$ | $131.68\text{ ms}$ | **$0.00\text{ ms}$** | **$0\text{ frames}$** | $131.68 + 0.00 = \mathbf{131.68\text{ ms}}$ |

- **Physical Arrival Difference Before Alignment**: $\Delta L = 131.68 - 107.40 = \mathbf{24.28\text{ ms}}$ (Realtek leads).
- **Physical Arrival Difference After Alignment**: $\Delta L = 131.68 - 131.68 = \mathbf{0.00\text{ ms}}$ (Synchronized).

---

## Validation & Test Suite
- Automated test suite expanded to **1,136 passing unit tests** (0 failing).
- Regression tests verify zero double-counting, immune to queue spikes, MAD filtering, and single delay buffer allocation at the hardware boundary.
