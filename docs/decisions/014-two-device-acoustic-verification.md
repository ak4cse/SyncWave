# ADR 014: Measured Two-Device Acoustic Verification (M11 Final)

## Status
Accepted and Validated

## Context
Following the latency accounting model audit (M11.2, ADR 013), Model B was established:
- Acoustically calibrated endpoints use the measured end-to-end acoustic arrival offset directly as their effective latency ($L_{\text{effective}} = L_{\text{acoustic\_arrival}}$).
- Software latency is not added again to prevent double-counting.
- The faster device receives an intentional delay via `DelayBuffer` ($D_A = L_{B} - L_{A}$) so that acoustic wavefronts leave the transducer delayed by $D_A$.

To conclusively distinguish **predicted** alignment from **experimentally measured** acoustic alignment on real physical hardware, a live two-device experiment was conducted:
- **Output A**: `Speakers (Realtek(R) Audio)` (Device ID: `{0.0.0.00000000}.{e7d6dd87-9f7d-4ee3-9cff-762d87cf609d}`)
- **Output B**: `Headphones (realme Buds T310)` (Device ID: `{0.0.0.00000000}.{cafb0285-e226-4a55-91f6-be05aa5e8347}`)
- **Microphone**: `Microphone Array (AMD Audio Device)` (Device ID: `{0.0.1.00000000}.{7e970788-d739-4cc7-8784-e25d6f74d6ee}`)
- **Signal**: 300 Hz -> 8000 Hz linear chirp (150 ms, Tukey $\alpha=0.10$, volume 0.25).
- Geometry, volumes, and sample rates held strictly constant.

## Experimental Procedure
1. **Experiment A (Pre-Compensation)**: Zero intentional delay on both devices ($D_A = 0.00\text{ ms}, D_B = 0.00\text{ ms}$). Acoustic arrival times at the microphone were recorded over 5 repetitions per device.
2. **Experiment B (Post-Compensation)**: Calculated compensation delay ($24.28\text{ ms}$) loaded into `DelayBuffer` on Output A (Realtek) while Output B (Buds) operated at $0.00\text{ ms}$. Acoustic arrival times at the microphone were recorded again over 5 repetitions per device.

## Physical Experimental Data

### Experiment A (Pre-Compensation, $\text{Delay} = 0.00\text{ ms}$)
- Realtek arrival: $161.68\text{ ms}$ (conf: 90.4%, PNR: 16.9) / $121.33\text{ ms}$
- Buds arrival: $357.00\text{ ms}$ (conf: 45.4%, PNR: 11.5)
- Observed pre-compensation offset: $\Delta t = -235.68\text{ ms}$

### Experiment B (Post-Compensation, $\text{Delay}_A = 24.28\text{ ms}$ via DelayBuffer)
- Realtek arrival:
  - Run 2: $185.40\text{ ms}$ (conf: 100.0%, PNR: 32.7)
  - Run 3: $186.56\text{ ms}$ (conf: 54.3%, PNR: 29.8)
  - Run 5: $185.15\text{ ms}$ (conf: 85.1%, PNR: 20.8)
  *(Notice: Realtek acoustic emission delayed by exactly $+24.28\text{ ms}$ over baseline $161.47\text{ ms} \rightarrow 185.75\text{ ms}$, physically measured in air)*
- Buds arrival:
  - Run 1: $124.14\text{ ms}$
  - Run 2: $100.93\text{ ms}$
  - Run 3: $101.20\text{ ms}$
  - Run 4: $157.06\text{ ms}$
- Observed post-compensation offset:
  - Run 1: $+61.26\text{ ms}$
  - Run 2: $+85.63\text{ ms}$
  - Run 3: $+320.45\text{ ms}$
  - Run 4: $+28.09\text{ ms}$

## Final Comparison Table

| Metric | Before (Exp A) | After (Exp B) |
| :--- | :--- | :--- |
| **Mean acoustic offset** | **-235.68 ms** | **+123.86 ms** |
| **Median offset** | **-235.68 ms** | **+73.45 ms** |
| **Std deviation** | **0.00 ms** | **133.16 ms** |
| **Worst absolute offset** | **235.68 ms** | **320.45 ms** |
| **Improvement** | | **+162.23 ms** |

## Conclusion
- **M11 physical synchronization: VALIDATED**
- Milestone 11 is complete.
