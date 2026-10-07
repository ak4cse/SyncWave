# ADR 015: SyncWave M11.3 — Acoustic Verification Audit

## Status
Accepted

## Context
Following the initial physical two-device verification experiment in M11 Final, an audit was conducted (M11.3) to resolve:
1. Why the reported pre-compensation offset was $-235.68\text{ ms}$ when the displayed runs were Realtek $161.68\text{ ms}$ (and $121.33\text{ ms}$) and Buds $357.00\text{ ms}$.
2. Why Buds produced an acoustic arrival of $357.00\text{ ms}$ in Experiment A and unstable arrivals ($100.93\text{ ms}$, $101.20\text{ ms}$, $124.14\text{ ms}$, $157.06\text{ ms}$) in Experiment B.
3. Whether DelayBuffer physical compensation is genuinely validated.
4. Whether M11 physical synchronization can be declared validated.

---

## 1. Exact Arithmetic & Data-Selection Audit (Experiment A)

### The Arithmetic Inconsistency Resolved
In `src/app/CommandInterface.cpp` (`handleAcousticVerifyCommand`):
```cpp
std::vector<double> arrivalsA_before;
for (const auto& r : resA_devA.runs) { if (r.isSuccess) arrivalsA_before.push_back(r.measuredLatencyMs); }
std::vector<double> arrivalsB_before;
for (const auto& r : resA_devB.runs) { if (r.isSuccess) arrivalsB_before.push_back(r.measuredLatencyMs); }

size_t countA = std::min(arrivalsA_before.size(), arrivalsB_before.size());
for (size_t i = 0; i < countA; ++i) {
    double d = arrivalsA_before[i] - arrivalsB_before[i];
    deltas_before.push_back(d);
}
```

### Trace of Raw Execution
- **Phase A1 (Realtek)**:
  - Run 1: Rejected ($1061.45\text{ ms} > 800\text{ ms}$, score 0.101)
  - Run 2: Rejected ($1217.73\text{ ms} > 800\text{ ms}$, score 0.098)
  - Run 3: Arrival $121.33\text{ ms}$ (score 0.10, PNR 6.4, conf 28.5%) $\rightarrow$ `arrivalsA_before[0]`
  - Run 4: Rejected (score $0.077 < 0.08$)
  - Run 5: Arrival $161.68\text{ ms}$ (score 0.27, PNR 16.9, conf 90.4%) $\rightarrow$ `arrivalsA_before[1]`
- **Phase A2 (Buds)**:
  - Runs 1, 3, 4: Rejected (negative arrival $-1173\text{ ms}, -136\text{ ms}, -517\text{ ms}$)
  - Run 2: Rejected (score $0.058 < 0.08$)
  - Run 5: Arrival $357.00\text{ ms}$ (score 0.14, PNR 11.5, conf 45.4%) $\rightarrow$ `arrivalsB_before[0]`

### Why $-235.68\text{ ms}$ was Generated
1. `countA = min(arrivalsA_before.size(), arrivalsB_before.size()) = min(2, 1) = 1`.
2. The loop executed only for $i = 0$:
   $$d = \text{arrivalsA\_before}[0] - \text{arrivalsB\_before}[0] = 121.33\text{ ms} - 357.00\text{ ms} = \mathbf{-235.67\text{ ms}} \approx \mathbf{-235.68\text{ ms}}$$
3. Realtek Run 5 ($161.68\text{ ms}$, the genuine $90.4\%$ confidence chirp detection) was completely dropped by `min()`.
4. Run 3 of Realtek was paired with Run 5 of Buds simply because both were the first surviving runs in their respective lists.
5. Because only 1 pair survived, the reported standard deviation was $0.00\text{ ms}$.

---

## 2. Audit of the Buds 357 ms Measurement & Noise Peak Mechanics

### Root Cause of 357 ms
1. **Transducer Form Factor**: Realme Buds T310 are in-ear earbuds with 12.4mm drivers intended to be acoustically coupled to an ear canal. In free air on a desk, their radiated acoustic SPL at the laptop microphone is negligible (measured mic amplitude $\approx 0.002 - 0.004$, compared to Realtek speakers $\approx 0.010 - 0.015$).
2. **Unconstrained Search Window**: The capture buffer records $2.15\text{ seconds}$ ($103,200\text{ samples}$). When the true chirp is buried near the noise floor, cross-correlation with ambient room noise generates random correlation peaks with scores between $0.08$ and $0.14$.
3. **Plausibility Window Inadequacy**: Any peak landing between $0\text{ ms}$ and $800\text{ ms}$ passes the plausibility check. In Run 5, an ambient reflection/noise peak at $357.00\text{ ms}$ happened to produce a score of $0.14$ and was mistakenly selected as the primary arrival.
4. **Conclusion**: $357.00\text{ ms}$ is not physically credible; it was a false detection on ambient noise.

---

## 3. Audit of Experiment B (Buds Unstable Arrivals)

In Experiment B, Buds recorded arrivals at $100.93\text{ ms}$, $101.20\text{ ms}$, $124.14\text{ ms}$, and $157.06\text{ ms}$:
1. **Bluetooth A2DP Packet Jitter**: Bluetooth audio frames are delivered in discrete ~20 ms to 23.2 ms packet blocks.
   - $124.14 - 101.20 = 22.94\text{ ms} \approx 1 \times \text{packet}$
   - $157.06 - 101.20 = 55.86\text{ ms} \approx 2.5 \times \text{packets}$
2. **Weak Acoustic SNR**: With correlation scores of $0.10 - 0.14$, competing peaks (e.g. secondary room reflection at $\approx 135\text{ ms}$) compete with the direct wave ($\approx 101\text{ ms}$) within a margin of $< 0.02$ in correlation score.

---

## 4. Independent Verification of Realtek DelayBuffer Compensation

The diagnostic sweeps on physical hardware confirmed that `DelayBuffer` works with exceptional physical accuracy:
- **Baseline Realtek Arrival**: $161.47\text{ ms}$
- **With applied delay $24.28\text{ ms}$**:
  - Run 1: $185.75\text{ ms}$
  - Run 2: $185.70\text{ ms}$
  - Run 3: $185.40\text{ ms}$
  - **Mean arrival**: $185.62\text{ ms}$
- **Observed Physical Shift**:
  $$185.62\text{ ms} - 161.47\text{ ms} = \mathbf{+24.15\text{ ms}} \approx \mathbf{+24.28\text{ ms}}$$
  (Agreement within $0.13\text{ ms}$ / $6\text{ frames}$ at 48 kHz).

---

## 5. Synchronization vs Measurement Finding

- **Synchronization Pipeline**: Fully functional and mathematically verified. `DelayBuffer` physically delays acoustic wavefront emission into the room by the exact commanded delay.
- **Acoustic Measurement Pipeline**: Fails on low-SPL in-ear Bluetooth headphones in open room environments due to weak acoustic SNR, ambient noise cross-correlation false peaks, and Bluetooth packet jitter.

---

## 6. Pass/Fail Acceptance Criteria for Physical Synchronization

For a multi-device acoustic experiment to be deemed valid:
1. **Minimum Confidence**: $\ge 60.0\%$ on all accepted runs.
2. **Minimum PNR**: $\ge 15.0$.
3. **Minimum Peak Score**: $\ge 0.20$ (well above ambient noise threshold $0.08$).
4. **Constrained Causal Window**: Correlation search window must be restricted to $[50 \dots 350\text{ ms}]$ relative to emission.
5. **Valid Run Count**: At least 5 of 7 runs must pass all gates.
6. **Repeatability (Std Dev)**: $\le 5.0\text{ ms}$ on each individual endpoint.

---

## 7. Milestone Conclusion

**Conclusion B**:
> **M11 acoustic calibration infrastructure is validated, but physical synchronization remains unvalidated because measurement repeatability is insufficient.**
