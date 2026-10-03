# ADR 006: Multiple Simultaneous WASAPI Outputs and Fan-Out Routing

## Context
In Milestones 1–5, SyncWave routed audio through a single WASAPI output renderer. To achieve synchronized multi-endpoint playback across heterogeneous devices (e.g., Bluetooth headphones, USB DACs, and built-in speakers), the engine must expand from a single-consumer pipeline to a concurrent fan-out architecture capable of feeding multiple physical endpoints simultaneously without cross-endpoint interference.

## Decision
We implemented a dedicated fan-out router (`OutputRouter`) and per-output pipeline abstractions (`DeviceOutput`):

1. **Single-Consumer Master Bus Integrity**:
   - `MasterAudioBus` remains strictly single-producer, single-consumer (SPSC lock-free ring buffer).
   - `OutputRouter` acts as the *sole* consumer of `MasterAudioBus`, pulling master audio frames and fanning them out to all destination outputs.
   - No direct multi-consumer contention or mutexes on the master audio bus.

2. **Per-Output Isolation (`DeviceOutput`)**:
   - Each active render endpoint is managed by an independent `DeviceOutput` pipeline containing:
     - Its own dedicated 1.0-second SPSC lock-free `RingBuffer` queue.
     - Its own dedicated `Resampler` configured for `(masterSampleRate -> deviceSampleRate)`.
     - Its own event-driven `WasapiOutput` instance running on an independent MMCSS real-time thread.
   - Slow or lagging consumers cannot block or stall fast consumers.
   - Non-destructive queue overflow handling: if an output queue becomes full, excess frames are safely dropped and recorded as `queueOverruns` without corrupting audio in other outputs.

3. **Multi-Sample-Rate Support**:
   - Master bus operates at canonical 48,000 Hz Float32 stereo (or loopback source rate).
   - Output endpoints running at differing native hardware rates (e.g., 44.1 kHz Bluetooth realme Buds T310 alongside 48.0 kHz Realtek integrated speakers) are continuously resampled independently via their dedicated `Resampler::pull()` pipeline.

4. **Dynamic Hot-Unplug Fault Tolerance**:
   - If an output device is disconnected or invalidated, `onDeviceDisconnected(deviceId)` immediately flags the output as unavailable (`isAvailable = false`) and closes its render client without interrupting or deadlocking remaining active outputs.

5. **Physical Synchronization Disclaimer**:
   - Milestone 6 establishes the multi-output routing and buffering infrastructure.
   - **No physical acoustic synchronization, delay alignment, or clock drift compensation is claimed or applied in Milestone 6.** Endpoints play simultaneously with independent hardware clock drifts and device latencies. Clock synchronization and drift compensation are scheduled for Milestones 7+.

## Consequences
- SyncWave can concurrently render system audio or synthetic tone across 2, 3, or more physical endpoints simultaneously.
- Zero underruns and zero queue overruns achieved across both Realtek (48 kHz) and realme Buds T310 (44.1 kHz) endpoints.
- Fully backwards-compatible CLI supporting `--output, -o` (repeatable) and `--outputs, -O` (comma-separated).
