# SyncWave Architecture Specification

SyncWave is designed with a strict three-plane architecture to ensure real-time audio safety, deterministic timing, and high maintainability.

```text
       +-------------------------------------------------------+
       |                      CLI Layer                        |
       |                   CommandInterface                    |
       +---------------------------+---------------------------+
                                   |
                                   v
       +-------------------------------------------------------+
       |                     Control Plane                     |
       |                     DeviceManager                     |
       |       +-------------------+-------------------+       |
       |       |                   |                   |       |
       |       v                   v                   v       |
       |  IMMDeviceEnum     DeviceNotification  SyncController |
       +-------+-------------------+-------------------+-------+
               |                   |
               v                   v
      [Audio Endpoints]     [Windows Core Audio]
      (Realtek, Buds, etc)  (Hot-Plug / State / Default events)
```

---

## 1. System Planes

### A. Control Plane
- **Responsibilities**: Device enumeration, configuration, lifecycle management, user command processing.
- **Classes**: `CommandInterface`, `DeviceManager`, `DeviceNotificationClient`.
- **Threading**: Invoked on application main thread and Windows RPC MTA notification callback threads.
- **Rules**: Not subject to real-time constraints; can safely perform COM allocations, error formatting, and mutex synchronization.

### B. Timing Plane *(Subsequent Milestones)*
- **Responsibilities**: Master clock timeline, endpoint clock monitoring, empirical latency estimation, drift calculation and correction.
- **Classes**: `MasterClock`, `LatencyEstimator`, `DriftEstimator`, `SyncController`.

### C. Audio Data Plane
- **Responsibilities**: High-priority real-time audio movement (WASAPI Loopback Capture -> RingBuffer -> DelayBuffer -> WASAPI Render Client).
- **Rules**: Lock-free, bounded memory, strictly **no allocations (`malloc`/`new`)**, no blocking mutexes, no file/console I/O, no COM queries on the audio render thread.

---

## 2. Milestone 2: Endpoint Notifications Architecture

### Component Hierarchy & Ownership
1. **`DeviceManager`** owns the COM `IMMDeviceEnumerator` instance.
2. When `DeviceManager::startMonitoring` is called:
   - A `DeviceNotificationClient` instance is instantiated.
   - It is registered with Windows Core Audio via `IMMDeviceEnumerator::RegisterEndpointNotificationCallback`.
   - Windows increments the reference count of `DeviceNotificationClient` via `AddRef()`.
3. When `DeviceManager::stopMonitoring` or destruction occurs:
   - `DeviceNotificationClient::setListener(nullptr)` is called atomically to immediately decouple callbacks.
   - `IMMDeviceEnumerator::UnregisterEndpointNotificationCallback` is called.
   - Windows invokes `Release()`, dropping the COM reference.
   - The COM smart pointer releases its reference, deleting the client.

### Threading Model
Windows Core Audio invokes `IMMNotificationClient` callbacks on internal system worker threads (RPC Multithreaded Apartment).
To ensure safety:
1. Callbacks do **not** perform console output or block on complex state.
2. The notification sink decodes Windows parameters (converting wide string device IDs to UTF-8) and forwards normalized `DeviceEvent` records.
3. In the CLI `syncwave watch` implementation, incoming events are enqueued into a thread-safe synchronized queue.
4. The CLI main thread waits on a condition variable with timeout, dequeuing events and rendering formatted output to stdout.

---

## 3. Milestone 3: Single WASAPI Output Renderer

### Component Hierarchy
```text
[CommandInterface]
        |
        v
  [AudioEngine]
   /         \
  v           v
[WasapiOutput] [ToneGenerator]
      |
      v
 [Render Thread] (MMCSS: "Pro Audio")
      |
      v
[IAudioRenderClient] -> Windows Audio Engine -> Hardware Endpoint
```

### Key Subsystems
1. **`AudioFormat`**: Clean internal data structure modeling sample rate, channel count, bit depth, block alignment, and sample type (`Float32`, `Int16`, `Int24In32`, `Int32`), completely free of Windows header dependencies.
2. **`ToneGenerator`**: Pure DSP sine generator with continuous phase tracking across chunks:
   $$\phi_{k+1} = (\phi_k + 2\pi f / f_s) \pmod{2\pi}$$
   Computes interleaved samples directly into the destination buffer without heap allocations.
3. **`WasapiOutput`**:
   - Opens target endpoint via `IMMDeviceEnumerator`.
   - Initializes `IAudioClient` with `AUDCLNT_SHAREMODE_SHARED | AUDCLNT_STREAMFLAGS_EVENTCALLBACK`.
   - Dedicated high-priority render thread registered with MMCSS (`AvSetMmThreadCharacteristicsW`).
   - Uses `IAudioClock` for playhead tracking.
   - Lock-free telemetry counters (`framesRendered`, `underruns`).
4. **`AudioEngine`**: Coordinates output lifecycle, device lookup, and signal generation.

---

## 4. Milestone 4: Master Audio Bus & Real-Time Ring Buffer

### Component Hierarchy
```text
           [ToneGenerator] (or Loopback Capture)
                  |
                  | write()
                  v
         +------------------+
         |  MasterAudioBus  |  (Canonical Format: Float32 Stereo)
         |   [RingBuffer]   |  (Preallocated Lock-Free SPSC)
         +------------------+
                  |
                  | read()
                  v
            [WasapiOutput] (MMCSS: "Pro Audio")
                  |
                  v
         [IAudioRenderClient] -> Hardware Endpoint
```

### Key Subsystems
1. **`RingBuffer`**:
   - Cache-line aligned (`alignas(64)`) monotonic atomic indices (`writeIndex_`, `readIndex_`).
   - Circular buffer memory with zero dynamic allocations in the real-time path.
   - Guaranteed silence zero-padding on underrun and non-destructive drop on overrun.
2. **`MasterAudioBus`**:
   - Central audio data timeline decoupled from specific hardware endpoints.
   - Bounded capacity (1.0s / 48,000 frames) providing jitter absorption.
   - Independent telemetry tracking (`framesWritten`, `framesRead`, `underruns`, `overruns`).

---

## 5. Milestone 5: WASAPI Loopback Capture & Real-Time Resampling

### Component Hierarchy
```text
           [Windows Audio Engine]
                     |
                     v
             [WasapiCapture] (MMCSS: "Capture" / "Audio")
                     |
                     | write()
                     v
         +------------------------+
         |     MasterAudioBus     |  (Float32 stereo at native capture sample rate)
         |      [RingBuffer]      |  (1.0s buffer capacity)
         +------------------------+
                     |
                     | pull()
                     v
                [Resampler]          (Linear interpolation with boundary continuity)
                     |
                     | read()
                     v
               [WasapiOutput]        (MMCSS: "Pro Audio")
                     |
                     v
             [Physical Device]
```

### Key Subsystems
1. **`WasapiCapture`**:
   - Loopback capture client initialized with `AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK`.
   - Drains packets via `IAudioCaptureClient::GetNextPacketSize` and `GetBuffer`.
   - Translates `AUDCLNT_BUFFERFLAGS_SILENT` packets into zero-padded silence.
   - Real-time conversion of integer PCM streams (Int16, Int32) to canonical Float32.
2. **`Resampler`**:
   - Converts between arbitrary sample rates (e.g., 48,000 Hz capture to 44,100 Hz output).
   - Zero dynamic allocations during audio streaming.
   - Pull-mode FIFO architecture guaranteeing exact requested frame counts to downstream renderers.
   - Preserves fractional phase and boundary samples across block boundaries to prevent clicks.
3. **`AudioEngine`**:
   - Orchestrates concurrent capture and render pipelines.
   - Clean shutdown with zero thread leaks and comprehensive telemetry reporting.

---

## 6. Milestone 6: Multiple Simultaneous WASAPI Outputs & OutputRouter

### Component Hierarchy
```text
           [Windows Audio Engine] (or ToneGenerator)
                     |
                     v
             [WasapiCapture] (MMCSS: "Capture" / "Audio")
                     |
                     | write()
                     v
         +------------------------+
         |     MasterAudioBus     |  (Single consumer: OutputRouter)
         |      [RingBuffer]      |  (1.0s buffer capacity)
         +------------------------+
                     |
                     | dispatch()
                     v
              [OutputRouter]         (Lock-free fan-out to all active queues)
             /       |        \
            /        |         \
           v         v          v
     [DeviceOutput 1] [DeviceOutput 2] [DeviceOutput N]
     +--------------+ +--------------+ +--------------+
     |  Queue 1     | |  Queue 2     | |  Queue N     | (SPSC 1.0s)
     |  Resampler 1 | |  Resampler 2 | |  Resampler N | (e.g. 48k->44.1k)
     | WasapiOutput | | WasapiOutput | | WasapiOutput | (Dedicated MMCSS)
     +--------------+ +--------------+ +--------------+
            |                |                |
            v                v                v
     [Physical HW 1]  [Physical HW 2]  [Physical HW N]
     (Realtek 48kHz)  (Buds 44.1kHz)   (USB DAC / Steam)
```

### Key Subsystems
1. **`OutputRouter`**:
   - Sole consumer of `MasterAudioBus`, preserving strict SPSC lock-free semantics on the master bus.
   - Fans out identical audio frames into independent destination queues concurrently.
   - Isolates consumers: a slow or stalling output cannot block or degrade playback on other outputs.
   - Non-destructive queue overflow protection: excess frames on full queues are safely dropped and tracked as `queueOverruns`.
   - Dynamic device disconnect fault tolerance (`onDeviceDisconnected`).
2. **`DeviceOutput`**:
   - Encapsulates target endpoint metadata, dedicated 1.0-second SPSC queue, dedicated `Resampler`, and `WasapiOutput` instance.
   - Per-endpoint real-time thread isolation running with independent MMCSS `"Pro Audio"` characteristics.
   - Complete per-device telemetry tracking: frames routed, frames consumed, frames resampled, frames submitted, queue overruns/underruns, and WASAPI underruns.
3. **Multi-Output CLI & Diagnostics**:
   - Repeated `-o <dev>` and comma-separated `--outputs <id1>,<id2>` syntax supported across `tone` and `capture` commands.
   - Diagnostics report comprehensive per-endpoint telemetry.

---

## 7. Milestone 7: Timing & Clock Measurement Infrastructure

### Component Hierarchy
```text
           [AudioEngine] / [Control Thread]
                  |
                  | sampleClocks() @ 10 Hz
                  v
            [OutputRouter]
           /              \
          v                v
   [DeviceOutput 1] [DeviceOutput 2]
          |                |
          v                v
    [DeviceClock 1]  [DeviceClock 2]
    (128-sample OLS) (128-sample OLS)
          \                /
           v              v
          [DriftEstimator]
                 |
                 +-> Pairwise Drift (ppm)
                 +-> Rate Ratio (A/B)
                 +-> Relative Offset (ms)
```

### Key Subsystems
1. **`WasapiClockSnapshot`**:
   - Snapshot querying `IAudioClock::GetPosition`, `IAudioClock::GetFrequency`, `IAudioClient::GetCurrentPadding`, and `IAudioClient::GetStreamLatency`.
   - Distinguishes device clock ticks, correlated QPC timestamp, engine padding, and stream latency.
2. **`DeviceClock`**:
   - Thread-safe sliding window (128 samples) per endpoint.
   - Ordinary Least Squares (OLS) linear regression calculates effective clock rate ($Hz$), deviation from nominal ($ppm$), and goodness of fit ($r^2$).
   - Rejects uninitialized snapshots and filters jitter before calculating rates.
3. **`DriftEstimator`**:
   - Calculates normalized clock rate ratio and relative drift in ppm:
     $$\text{Relative Drift (ppm)} = \left(\frac{r_A}{r_B} - 1\right) \times 10^6$$
   - Calculates relative playhead offset in milliseconds ($T_A - T_B$).
4. **Zero-Contention Real-Time Safety**:
   - WASAPI audio render threads remain strictly lock-free; clock sampling is initiated out-of-band at 10 Hz by control/producer loops.
5. **Acoustic Disclaimer**:
   - Distinguishes software/WASAPI domain latency from physical acoustic emission latency (Bluetooth A2DP transport buffer, DAC filtering, speaker driver lag).

---

## 8. Milestone 8: Relative Latency & Timing Model Validation

### Component Hierarchy
```text
           [AudioEngine] / [CLI Commands]
           |
           +---> [syncwave clock-test]   (60s multi-window convergence)
           +---> [syncwave latency-test] (SyncPulseGenerator repeatability)
           |
           v
     [SyncPulseGenerator]
           |
           | 500ms silence -> 1ms half-sine -> 500ms silence
           v
    [MasterAudioBus] (Master Timeline Tracking: masterTimelineFrames)
           |
           v
     [OutputRouter]  (Windowed Drift Queries @ 1s, 5s, 10s, 30s, 60s)
    /              \
   v                v
[DeviceOutput 1] [DeviceOutput 2]
+--------------+ +--------------+
| Playhead Est | | Playhead Est |  (App Playhead: submitted - padding)
| DeviceClock  | | DeviceClock  |  (1024-sample capacity, multi-window OLS)
+--------------+ +--------------+
       \                /
        v              v
       [DriftEstimator]
              |
              +-> Instantaneous Offset (posA - posB)
              +-> Initial Offset (posA0 - posB0)
              +-> Accumulated Drift (Offset_t - Offset_0)
              +-> Drift Rate (ppm from Delta Offset)
              +-> Confidence Metric (rA^2 * rB^2)
```

### Key Subsystems
1. **Multi-Window Rate Estimation & Convergence Analysis**:
   - `DeviceClock::estimateRateOverWindow(windowSec)` evaluates rate stability across trailing windows (1s, 5s, 10s, 30s, 60s).
   - Resolved the Bluetooth -612 ppm loopback artifact: proved that initial A2DP buffer ramp (~190 ms) skews short regression slopes, whereas steady-state linear regression converges (+1.08 ppm at 10s).
2. **Mathematical Separation of Offset vs Drift**:
   - Strictly distinguishes instantaneous playhead differences ($T_A - T_B$) from accumulated drift ($\Delta\text{Offset}$) and drift rate ($\text{ppm}$).
   - Proven that constant offset produces exactly 0.0 ms accumulated drift.
3. **Stream Playhead Estimation**:
   - `DeviceOutput` calculates software playhead:
     $$\text{App Playhead} = \frac{\text{framesSubmitted} - \text{currentPadding}}{f_s}$$
     $$\text{WASAPI Clock Playhead} = \frac{\text{clockPosition}}{\text{clockFrequency}}$$
   - Captures playhead discrepancy between application-level buffer tracking and hardware clock reports.
4. **Deterministic Transient Test Harness (`SyncPulseGenerator`)**:
   - Generates bandlimited half-cycle sine pulses (1.0 ms duration, peak 1.0f) with 500 ms lead-in and lead-out silence.
   - Pushes pulses through `MasterAudioBus` and `OutputRouter` into all configured endpoints concurrently.
   - Evaluates software-path relative latency repeatability across repeated runs.
5. **Physical Acoustic Latency Honesty Statement**:
   - Documents that software timestamps (`IAudioClock`, QPC, and padding) observe only OS audio engine mixing and driver submission stages.
   - Physical acoustic emission includes external physical delays (Bluetooth A2DP transport packetization, RF buffering, hardware DAC delay, and acoustic travel time).
   - Direct measurement of physical acoustic latency requires an external calibrated microphone feedback loop or oscilloscope probe.

---

## 9. Milestone 9: Fixed Software-Domain Output Alignment & Per-Output Delay Stage

### Component Hierarchy
```text
                     [AudioEngine] / [CLI Commands]
                                   |
                  +----------------+----------------+
                  |                                 |
                  v                                 v
         [SyncController]                  [syncwave calibrate]
         +--------------------------+      +--------------------------+
         | calculateTargetLatency() |      | Probe Endpoint Latencies |
         | calculateDeviceDelay()   |      | Print Latency Breakdown  |
         | computeSoftwarePlan()    |      | Print Acoustic Disclaimer|
         | computeManualPlan()      |      +--------------------------+
         +--------------------------+
                  |
                  | applySyncPlan()
                  v
            [OutputRouter]
           /              \
          v                v
   [DeviceOutput 1] [DeviceOutput 2]
   +--------------+ +--------------+
   | SPSC Queue   | | SPSC Queue   | (Canonical 48.0 kHz)
   | Resampler    | | Resampler    | (48.0 kHz -> f_dev)
   | DelayBuffer  | | DelayBuffer  | (f_dev Native Frames, Circular, RT-Safe)
   | WASAPIClient | | WASAPIClient | (f_dev Hardware Render)
   +--------------+ +--------------+
```

### Key Subsystems
1. **`DelayBuffer`**:
   - Placed **after** resampling at native device sample rates ($f_{\text{dev}}$).
   - Circular float buffer for interleaved stereo PCM with initial silence prefill.
   - Atomic lock-free `delayFrames_` parameter update for real-time safety.
   - Zero allocation and constant-time execution in steady state; zero-delay passthrough optimization.
2. **`OutputLatencyModel`**:
   - Latency taxonomy: $L_{\text{stream}}$, $L_{\text{pad}}$, $L_{\text{queue}}$, $L_{\text{resamp}}$, $L_{\text{delay}}$, and $L_{\text{cal}}$.
   - Estimated software latency: $L_{\text{sw}} = L_{\text{stream}} + L_{\text{pad}} + L_{\text{queue}} + L_{\text{resamp}}$.
   - Effective latency: $L_{\text{eff}} = L_{\text{sw}} + L_{\text{cal}}$.
   - Synchronization state tracking: `Disabled`, `Manual`, `SoftwareCalibrated`, `PhysicallyCalibrated`, `Uncertain`.
3. **`SyncController`**:
   - Slowest path baseline: $T_{\text{target}} = \max_i(L_{\text{eff}, i})$.
   - Compensation delay: $D_i = \max(0.0, T_{\text{target}} - L_{\text{eff}, i})$.
   - Frame conversion at native device rate: $D_{\text{frames}, i} = \text{round}(D_i \times f_{\text{dev}, i} / 1000)$.
4. **CLI Commands & Delay Options**:
   - `syncwave calibrate`: Probes endpoints, computes alignment plan, prints breakdown table, and details acoustic disclaimer.
   - `--delay <d0,d1>`: Explicit per-output manual delays in ms.
   - `--sync software`: Automatic software-domain alignment.
   - `--offsets <o0,o1>`: Manual physical calibration offsets.

---

## 10. Milestone 10: Controlled Long-Term Clock Drift Correction

### Component Hierarchy
```text
                    [AudioEngine] / [Control Thread @ 10 Hz]
                                       |
                                       | sampleAllClocks() / updateDriftCorrection()
                                       v
                                 [OutputRouter]
                                /              \
                               v                v
                        [DeviceOutput 1] [DeviceOutput 2]
                        +-----------------------------------------------+
                        | SPSC Queue (1.0s, canonical 48 kHz Float32)   |
                        |                                               |
                        | [FilteredDriftEstimator]                      |
                        |   - Windowed OLS regression                   |
                        |   - Min observation check (>=5.0s, >=30 pts)  |
                        |   - Outlier rejection (|drift|>500ppm, r^2<0.9|
                        |   - Dual EMA smoothing (alpha_drift=0.15)     |
                        |                                               |
                        | [DriftController]                             |
                        |   - DriftCorrectionState state machine        |
                        |   - +/-1.0 ms deadband                        |
                        |   - Proportional control (Kp = 10.0 ppm/ms)   |
                        |   - Feedforward cancellation (-drift)         |
                        |   - Hard clamp (+/-100 ppm)                   |
                        |                                               |
                        | [Resampler]                                   |
                        |   - Micro-resampling rate adjustment          |
                        |   - Slew rate limiter (<= 5.0 ppm/s)          |
                        |   - Continuous per-frame linear interpolation |
                        |                                               |
                        | [DelayBuffer] (Fixed alignment stage)         |
                        |                                               |
                        | [WasapiOutput] (MMCSS "Pro Audio")            |
                        +-----------------------------------------------+
                               |                |
                               v                v
                        [Physical HW 1]  [Physical HW 2]
```

### Key Subsystems
1. **`Resampler` Micro-Rate Adjustment & Slew Rate Limiter**:
   - Software-domain micro-resampling adjustments parameterized by target PPM:
     $$R_{\text{eff}} = R_{\text{base}} \times \left(1 + \frac{\text{ppm}}{10^6}\right)$$
   - Continuous per-frame linear ratio interpolation: smoothly slews `currentAdjustmentPpm_` toward `targetAdjustmentPpm_` at a bounded rate ($\le 5.0\text{ ppm/s}$).
   - Prevents sudden pitch shifts, clicks, or phase discontinuities.
   - When $\text{ppm} \ne 0$, bypasses memory copy optimizations to ensure deterministic fractional phase progression.
   - Independent of hardware driver capabilities (does not rely on `IAudioClockAdjustment`).
2. **`FilteredDriftEstimator`**:
   - Maintains windowed OLS regression over valid `WasapiClockSnapshot` history.
   - Enforces strict qualification criteria: minimum 5.0 seconds observation and $\ge 30$ data points before producing confident drift estimates.
   - Multi-stage outlier rejection: rejects unphysical deviations ($|\text{drift}| > 500\text{ ppm}$), poor linear fit ($r^2 < 0.90$), and sudden rate jumps ($> 375\text{ ppm}$).
   - Dual Exponential Moving Average (EMA) filters for drift rate ($\alpha = 0.15$) and phase error ($\alpha = 0.20$) to eliminate high-frequency jitter.
3. **`DriftController`**:
   - State machine tracking: `Disabled`, `Initializing`, `Measuring`, `Locked`, `Correcting`, `Uncertain`, `Disconnected`.
   - Deadband ($\pm 1.0\text{ ms}$): prevents unnecessary control activity under minor timing jitter.
   - Combined Feedforward and Proportional Feedback Control:
     $$u_{\text{FF}} = -D_{\text{filtered}}$$
     $$u_P = -K_p \times \text{sign}(e) \times (|e| - \text{deadband}) \quad (\text{for } |e| > \text{deadband})$$
     $$u(t) = \text{clamp}(u_{\text{FF}} + u_P, -100, +100)$$
4. **Zero-Contention Real-Time Safety**:
   - The real-time render thread performs only slewed ratio evaluation and sample interpolation; no mutex locks, heap allocations, or floating-point transcendental functions occur on the audio callback path.
   - Control calculations are executed asynchronously on the 10 Hz control loop.
5. **Physical Acoustic Disclaimer**:
   - Maintains the clear distinction between software buffer alignment / clock rate drift and external physical acoustic latency (RF packetization, DAC filters, speaker room propagation).
