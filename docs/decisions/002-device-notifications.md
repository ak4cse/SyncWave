# ADR 002: Real-Time Endpoint Notifications via IMMNotificationClient

## Context
SyncWave must respond dynamically to audio device lifecycle events:
- Bluetooth audio endpoints disconnecting or reconnecting during playback.
- Wired headphones being plugged or unplugged.
- Default render endpoint being switched by the user or operating system.

Two approaches were evaluated:
1. **Periodic Polling**: Periodically calling `IMMDeviceEnumerator::EnumAudioEndpoints` (e.g. every 500 ms) and comparing snapshots.
2. **Native COM Callback (`IMMNotificationClient`)**: Registering a callback interface with `IMMDeviceEnumerator::RegisterEndpointNotificationCallback`.

## Decision
We implemented native event-driven notifications using `IMMNotificationClient`:
1. **No Polling Overhead**: Zero CPU usage when no hardware changes occur. Immediate notification latency (< 5 ms) when devices change.
2. **Standard COM Reference Counting**: `DeviceNotificationClient` implements thread-safe `AddRef()` and `Release()` using `std::atomic<ULONG>`, matching COM specifications.
3. **Decoupled Architecture**:
   - `DeviceNotificationClient` implements `IMMNotificationClient` and forwards notifications to `INotificationListener`.
   - `DeviceManager` owns the registration lifecycle and translates raw Windows IDs and DWORD state masks into clean, high-level `DeviceEvent` structs (`Added`, `Removed`, `StateChanged`, `DefaultChanged`, `PropertyChanged`).
   - The CLI consumes these events without referencing any Windows COM types.
4. **MTA Thread Safety**: Windows delivers notifications on RPC MTA worker threads. The CLI listener enqueues events into a thread-safe synchronized queue, preventing deadlock with console stdout.

## Shutdown Sequence
To avoid use-after-free or dangling callbacks during process shutdown or `Ctrl+C`:
1. `DeviceNotificationClient::setListener(nullptr)` is atomically set.
2. `IMMDeviceEnumerator::UnregisterEndpointNotificationCallback` unregisters the sink with Windows.
3. Windows releases its reference (`Release()`).
4. `DeviceManager` releases its reference, cleanly destroying the client.
