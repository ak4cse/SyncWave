# ADR 009: Fixed Software-Domain Output Alignment & Per-Output Delay Stage

## Context

Following the completion of Milestone 7 (clock rate estimation and pairwise drift observation) and Milestone 8 (timing model validation, multi-window convergence analysis, playhead estimation, and deterministic transient testing), SyncWave entered the first active synchronization phase.

Prior to Milestone 9, all physical endpoints received samples as quickly as possible from their respective SPSC queues. Because different audio endpoints have heterogeneous pipeline latencies—ranging from ~20 ms for onboard Realtek DACs to ~190+ ms for Bluetooth A2DP transport buffers—unaligned concurrent playback causes severe acoustic echo and comb filtering.

Milestone 9 introduces fixed software-domain output delay compensation while strictly maintaining real-time safety, zero-allocation audio callbacks, and truthful physical latency boundaries.

## Decisions

### 1. Delay Stage Placement Decision: Post-Resampler

The signal path for each output endpoint is structured as:
```text
MasterAudioBus (48.0 kHz Canonical Master)
                  │
                  ▼
         OutputRouter (Fan-Out)
                  │
                  ▼
      Per-Output SPSC Queue (48.0 kHz)
                  │
                  ▼
        Linear Resampler (48.0 kHz -> f_dev)
                  │
                  ▼
       DelayBuffer (f_dev Native Frames)
                  │
                  ▼
      WASAPI Render Client (f_dev Hardware)
```

**Rationale**:
- **Native Frame Accuracy**: Delaying after resampling applies delay directly in native device frames ($D_{\text{frames}} = \text{round}(D_{\text{ms}} \times f_{\text{dev}} / 1000)$).
- **No Resampler Transient Artifacts**: Passing delayed silence through the resampler would create startup filter transients. Resampling first ensures the resampler operates continuously on active master audio while the delay buffer handles pure temporal postponement.
- **Independence from Master Clock**: Each endpoint can be adjusted by exact integer device frames without fractional interpolation error.

### 2. DelayBuffer Architecture & Real-Time Safety

`DelayBuffer` is implemented as a preallocated circular stereo Float32 buffer:
- **Preallocation**: Allocated during endpoint initialization (capacity up to 5.0 seconds at device sample rate, e.g. 240,000 frames).
- **Initial Silence Prefill**: The circular buffer is initialized with zeros so that the first $D$ frames rendered to WASAPI are clean silence, seamlessly pushing initial playback out by exactly $D$ frames.
- **Lock-Free Parameter Updates**: The delay parameter `delayFrames_` is stored as `std::atomic<size_t>` with relaxed memory ordering, allowing dynamic delay reconfiguration without locking the audio render thread.
- **Zero-Delay Passthrough**: When $D = 0$, `process()` performs an immediate memory copy or in-place passthrough, incurring zero delay overhead.
- **Steady-State RT Safety**: Strictly zero heap allocations, zero system calls, zero mutexes, and $O(N)$ frame processing time inside the WASAPI render callback.

### 3. Auditing the Latency Taxonomy

SyncWave audits and separates all observable latency components into `OutputLatencyModel`:

| Component | Symbol | Source / Measurement | Typical Values |
|---|---|---|---|
| **WASAPI Stream Latency** | $L_{\text{stream}}$ | `IAudioClient::GetStreamLatency()` | 0.0 ms (event-driven shared) to 10.0 ms |
| **WASAPI Current Padding** | $L_{\text{pad}}$ | `IAudioClient::GetCurrentPadding()` / $f_{\text{dev}}$ | 10.0 ms to 25.0 ms (e.g. 1056 frames @ 48 kHz = 22.0 ms) |
| **Router SPSC Queue** | $L_{\text{queue}}$ | `RingBuffer::availableToRead()` / $f_{\text{master}}$ | 0.0 ms to 160.0 ms (startup buffer fill) |
| **Resampler Group Delay** | $L_{\text{resamp}}$ | Filter delay ($~0.5 / f_{\text{dev}}$) | ~0.01 ms (negligible in linear phase) |
| **Configured Delay** | $L_{\text{delay}}$ | `DelayBuffer::delayMs()` | 0.0 ms to 500.0 ms |
| **Calibration Offset** | $L_{\text{cal}}$ | User physical acoustic measurement | Manual input (e.g. +50.0 ms) |

- **Estimated Software Latency**:
  $$L_{\text{sw}} = L_{\text{stream}} + L_{\text{pad}} + L_{\text{queue}} + L_{\text{resamp}}$$
- **Effective Latency (Baseline for Alignment)**:
  $$L_{\text{eff}} = L_{\text{sw}} + L_{\text{cal}}$$

### 4. Alignment Mathematics: Slowest Path as Target

Audio cannot travel backward in time without an acoustic crystal ball. Therefore, the target alignment latency across $N$ active output endpoints is governed by the slowest endpoint:

$$T_{\text{target}} = \max_{i \in \{1 \dots N\}} (L_{\text{eff}, i})$$

The required compensation delay for device $i$ is:

$$D_i = \max(0.0, T_{\text{target}} - L_{\text{eff}, i})$$

The native frame delay applied to device $i$'s `DelayBuffer` is:

$$D_{\text{frames}, i} = \text{round}\left(D_i \times \frac{f_{\text{dev}, i}}{1000}\right)$$

**Consequences**:
- The slowest device receives $D = 0$ ms (baseline path).
- Faster devices receive positive delay ($D_i > 0$) to delay their emissions until the slowest device catches up.
- Negative delays are strictly clamped to 0.0 ms.

### 5. Synchronization State Machine

The system tracks explicit synchronization states (`SyncState`):
- `Disabled`: No delay alignment active (all $D_i = 0$).
- `Manual`: User-configured static delays applied via `--delay`.
- `SoftwareCalibrated`: Automatically aligned based on measured software-domain latency models via `--sync software` or `syncwave calibrate`.
- `PhysicallyCalibrated`: Manual physical calibration offsets (`--offsets`) combined with software latency models.
- `Uncertain`: Measurement failed or was too noisy to determine safe alignment.

### 6. User Interface & CLI Integration

- **`syncwave calibrate`**: Subcommand that probes target endpoints, inspects WASAPI stream latency and padding, computes the software alignment plan, prints a detailed breakdown table, and displays the physical acoustic disclaimer.
- **`--delay <d0,d1,...>`**: Allows configuring explicit manual delays in milliseconds per output across `tone`, `capture`, and `latency-test`.
- **`--sync <software|manual|none>`**: Selects synchronization mode.
- **`--offsets <o0,o1,...>`**: Provides manual physical calibration offsets in milliseconds.

### 7. Physical Acoustic Latency Disclaimer & Physical Calibration

Software timestamps and WASAPI stream latencies measure only the operating system and driver submission buffers. Physical acoustic emission involves external physical delays:
1. Bluetooth A2DP transport packetization, RF transmission, and headphone receiver buffer (~100–250 ms depending on SBC/AAC/LDAC codec profiles and buffer depths).
2. Hardware DAC reconstruction filters and amplifier circuitry (~1–5 ms).
3. Transducer electromechanical latency and room air flight time (~1 ms per 34.3 cm).

SyncWave explicitly disclaims claiming that software alignment equals physical acoustic alignment. Software alignment guarantees that audio frames arrive at the Windows driver boundary at the calculated target times; physical acoustic alignment requires manual offset input (`--offsets`) from external acoustic measurement (e.g. microphone transient capture).

### 8. Milestone Scope Boundaries

- **NO adaptive micro-resampling or dynamic clock drift correction** (scheduled for Milestone 10).
- **NO feedback control loops or `IAudioClockAdjustment`**.
- **NO GUI**.
- All render threads remain lock-free, zero-allocation, and real-time safe.

## Consequences

- SyncWave now possesses a frame-accurate, post-resampler delay stage capable of arbitrary static delay insertion up to 5,000 ms.
- Endpoints with different native sample rates (e.g., 44.1 kHz Bluetooth buds and 48.0 kHz onboard speakers) are correctly delayed in their respective native frame rates.
- The system provides complete transparency regarding software-domain vs physical acoustic latency.
