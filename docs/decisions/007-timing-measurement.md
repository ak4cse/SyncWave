# ADR 007: Timing & Clock Measurement Infrastructure

## Context
In Milestone 6, SyncWave established multi-output routing capable of feeding multiple physical endpoints concurrently. However, heterogeneous audio hardware (such as Bluetooth headphones, USB DACs, and integrated audio codecs) run on independent physical oscillators with manufacturing tolerances, thermal variance, differing driver buffer models, and distinct transport mechanisms.

Before any delay compensation or clock drift correction can be safely applied (Milestones 8+), SyncWave requires a robust measurement and modeling foundation to observe endpoint clock positions, effective clock rates, relative clock drift (ppm), and WASAPI stream latencies without disrupting real-time audio threads.

## Decision

1. **Low-Level WASAPI Timing Telemetry (`WasapiClockSnapshot`)**:
   - Extended `WasapiOutput` with `getClockSnapshot()` querying:
     - `IAudioClock::GetPosition`: device tick position paired with correlated `QueryPerformanceCounter` (QPC) timestamp.
     - `IAudioClock::GetFrequency`: hardware clock frequency in ticks per second (e.g. 384,000 Hz or 48,000 Hz).
     - `IAudioClient::GetCurrentPadding`: queued frames in the WASAPI engine buffer.
     - `IAudioClient::GetStreamLatency`: stream latency in 100-nanosecond units (`REFERENCE_TIME`).

2. **Sliding-Window Linear Regression Clock Modeling (`DeviceClock`)**:
   - Implemented `DeviceClock` maintaining a thread-safe sliding window (128 samples) of valid clock snapshots.
   - Employed Ordinary Least Squares (OLS) linear regression over cumulative frames versus elapsed steady-clock time:
     $$\text{Estimated Rate } (\text{Hz}) = \frac{\sum (t_i - \bar{t})(p_i - \bar{p})}{\sum (t_i - \bar{t})^2}$$
   - Evaluated goodness of fit ($r^2$) to filter transient jitter.
   - Calculated nominal clock deviation in parts per million (ppm):
     $$\text{Rate Error (ppm)} = \left(\frac{R_{\text{obs}}}{R_{\text{nom}}} - 1\right) \times 10^6$$

3. **Pairwise Drift & Offset Estimation (`DriftEstimator`)**:
   - Implemented `DriftEstimator` computing normalized rate ratios and pairwise relative drift:
     $$\text{Relative Drift (ppm)} = \left(\frac{r_A}{r_B} - 1\right) \times 10^6, \quad \text{where } r = \frac{R_{\text{obs}}}{R_{\text{nom}}}$$
   - Computed instantaneous relative stream position offset in milliseconds ($T_A - T_B$).

4. **Zero-Contention Real-Time Safety**:
   - Render threads in `WasapiOutput` remain strictly lock-free and dedicated to audio pumping.
   - Clock sampling is performed out-of-band at 10 Hz (every 100 ms) by control/producer threads (`AudioEngine` producer loop or CLI wait loops).
   - Linear regression computation occurs entirely outside audio render callbacks.

5. **Strict Domain Distinctions & Acoustic Disclaimer**:
   - Clarified distinction between:
     - **Offset**: Fixed time difference between streams ($ms$).
     - **Drift**: Continuous rate divergence over time ($ppm$).
     - **Latency**: Total transit time from emission to audibility ($ms$).
   - Explicitly documented that WASAPI stream latency and software timestamps measure only the software and audio engine domain. In event-driven shared mode, `GetStreamLatency` returns 0 ms while queue padding (`GetCurrentPadding`) reflects software buffer depth. Physical acoustic latency (Bluetooth A2DP encoding/packetization, RF buffering, hardware DAC delay, and acoustic travel time) is distinct and cannot be determined from software clocks alone.

6. **Observation-Only Policy**:
   - **No latency compensation, delay alignment, or drift correction is applied in Milestone 7.** This milestone strictly observes, measures, and logs timing telemetry.

## Consequences
- SyncWave accurately measures physical endpoint clock rates and relative drift across heterogeneous devices without audio dropouts or priority inversion.
- Real hardware validation demonstrated:
  - `Speakers (Realtek(R) Audio)` running at ~47,999.5 Hz (-11 ppm nominal error).
  - `Headphones (realme Buds T310)` running at ~48,000.3 Hz (+7 ppm nominal error under tone generation).
  - Pairwise relative drift of ~+18.2 ppm measured over a 30-second continuous dual-playback session.
- Ready foundation for Milestone 8 (delay buffer & latency alignment).
