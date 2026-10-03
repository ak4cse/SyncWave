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
