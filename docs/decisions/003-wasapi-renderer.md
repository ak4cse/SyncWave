# ADR 003: WASAPI Shared Event-Driven Output Renderer

## Context
SyncWave requires low-latency, deterministic, and reliable audio output across diverse Windows audio endpoints (Bluetooth, USB, integrated sound cards). The renderer must interact directly with the hardware endpoints without blocking the application thread or introducing high CPU overhead.

Two primary WASAPI operating modes were evaluated:
1. **Pull Mode (Event-Driven)** (`AUDCLNT_STREAMFLAGS_EVENTCALLBACK`): The Windows audio engine signals a synchronization event when the endpoint buffer requires new frames. The render thread waits on `WaitForMultipleObjects`.
2. **Push Mode (Timer-Driven / Polling)**: The application sleeps or polls periodically (e.g. using `Sleep` or multimedia timers) to query padding and push frames.

## Decision
We chose **Event-Driven Shared Mode** (`AUDCLNT_SHAREMODE_SHARED | AUDCLNT_STREAMFLAGS_EVENTCALLBACK`):
1. **Zero Busy-Waiting**: The render thread yields execution completely until signaled by the OS audio engine or a stop request (`WaitForMultipleObjects`), keeping CPU utilization near 0%.
2. **MMCSS Priority Elevation**: The render thread is registered with the Multimedia Class Scheduler Service via `AvSetMmThreadCharacteristicsW(L"Pro Audio", ...)` to prevent priority inversion or audio glitches under system load.
3. **Real-Time Safety**:
   - Strictly **no** dynamic memory allocation (`malloc`/`new`), blocking mutexes, or console I/O inside the render loop.
   - Telemetry (frames rendered, buffer underruns, clock position) is updated lock-free via `std::atomic`.
   - Audio generation (`ToneGenerator`) computes samples directly into the buffer provided by `IAudioRenderClient::GetBuffer`.
4. **Clean COM Lifecycle**:
   - The render thread initializes its own MTA COM apartment (`CoInitializeEx`).
   - Stop requests trigger a dedicated Win32 manual-reset event, ensuring prompt thread exit within milliseconds.
   - All COM interfaces (`IAudioRenderClient`, `IAudioClock`, `IAudioClient`, `IMMDevice`) are released cleanly via RAII ComPtr smart pointers.

## Consequences
- Single render endpoints can be opened, initialized, played, and stopped independently.
- The architecture is ready to scale to multi-output rendering in Milestone 6 by instantiating multiple `WasapiOutput` instances managed by an output manager.
