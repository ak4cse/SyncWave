# ADR 004: Central Real-Time Master Audio Bus & Lock-Free SPSC Ring Buffer

## Context
In early milestones, audio frames flowed directly from `ToneGenerator` into `WasapiOutput`. To enable multi-endpoint synchronization, loopback capture, and delay compensation, SyncWave requires a central audio data path that represents the authoritative master audio timeline independent of any specific output hardware.

## Decision
We implemented a dedicated **`MasterAudioBus`** backed by a lock-free **`RingBuffer`**:

1. **Lock-Free Single-Producer / Single-Consumer (SPSC)**:
   - Uses atomic monotonic `writeIndex_` and `readIndex_` with cache-line alignment (`alignas(64)`) to eliminate false sharing across CPU cores.
   - Producer and consumer advance indices using acquire-release memory order semantics (`std::memory_order_release` / `std::memory_order_acquire`).
   - Zero dynamic memory allocations during playback; storage is preallocated once during initialization.
2. **Canonical Master Audio Format**:
   - The canonical master audio format is defined as **Float32, 48,000 Hz, stereo**.
   - Output endpoints pull from this central bus. In this milestone, the bus adapts dynamically to the target endpoint format (e.g. 44.1 kHz for Bluetooth headphones, 48 kHz for integrated Realtek audio) with a 1.0 second buffer capacity (44,100 to 48,000 frames) providing robust headroom against transport jitter.
3. **Deterministic Underrun & Overrun Policy**:
   - **Underrun**: If a consumer requests more frames than are currently available, the ring buffer returns the available frames and zero-pads the remaining destination buffer with silence. The deficit is counted in `underruns_`. No uninitialized memory is ever read.
   - **Overrun**: If a producer attempts to write more frames than free capacity, the ring buffer writes up to available capacity and drops excess frames. The dropped frames are counted in `overruns_`. Old unread audio is never corrupted.
4. **Decoupled Architecture**:
   - Audio sources (`ToneGenerator`, and future WASAPI loopback capture) only write to `MasterAudioBus`.
   - Audio renderers (`WasapiOutput`) only read from `MasterAudioBus`.

## Consequences
- Audio generation and hardware rendering are completely isolated from each other in separate threads.
- Paves the way for Milestone 5 (WASAPI Loopback Capture) and Milestone 6 (Multi-Endpoint Output Rendering).
