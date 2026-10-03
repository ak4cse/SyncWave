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

### C. Audio Data Plane *(Subsequent Milestones)*
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

### Supported Events
- `OnDeviceAdded`: Fires when an audio device is plugged in or paired.
- `OnDeviceRemoved`: Fires when an audio device is unplugged or unpaired.
- `OnDeviceStateChanged`: Fires when a device transitions between `Active`, `Disabled`, `NotPresent`, and `Unplugged`.
- `OnDefaultDeviceChanged`: Fires when the system default render device changes (filtered for `eRender` flow and `eConsole`/`eMultimedia` roles).
- `OnPropertyValueChanged`: Fires when device properties (such as friendly name or format) change.
