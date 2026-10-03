# SyncWave — Implementation Plan

## 1. Project Goal

SyncWave is a Windows desktop application that plays the same audio through multiple independent audio devices while keeping their physical playback synchronized.

Example:

- Bluetooth earbuds
- Bluetooth speaker
- USB headset
- Wired 3.5 mm headphones

The core problem is not simply routing audio to multiple outputs. Different devices have different:

- output latency
- buffering
- Bluetooth transport delay
- sample clocks
- jitter
- sample rates
- channel layouts
- reconnect behavior

SyncWave therefore acts as a real-time synchronization engine between heterogeneous audio endpoints.

The long-term goal is:

```text
Application / Media Source
          |
          v
   Master Audio Timeline
          |
          v
    SyncWave Audio Bus
      /    |     |    \
     /     |     |     \
WASAPI  WASAPI WASAPI WASAPI
Out #1  Out #2 Out #3 Out #4
   |       |      |      |
Device A Device B Device C Device D
```

For a later phase:

```text
Video Clock
     |
     v
A/V Sync Controller
     |
Master Audio Timeline
     |
SyncWave Output Synchronization
```

---

# 2. Initial Scope

The first version should NOT attempt to solve everything.

## MVP

The MVP should:

1. Enumerate Windows audio output devices.
2. Detect device connection/disconnection.
3. Capture system audio.
4. Store audio in a central master buffer.
5. Render the same audio to at least two devices simultaneously.
6. Add configurable per-device delay.
7. Display device latency and synchronization information.
8. Recover from device disconnect/reconnect.

## Later versions

After the MVP:

- automatic latency calibration
- clock drift measurement
- adaptive drift correction
- sample-rate conversion
- automatic synchronization
- long-duration synchronization
- VLC integration
- audio/video synchronization
- acoustic calibration
- polished GUI

---

# 3. Platform and Technology

## Target platform

Windows 10/11.

Windows is the first target because WASAPI and Windows Core Audio provide direct access to:

- render endpoints
- audio clients
- render buffers
- audio clocks
- endpoint notifications
- loopback capture
- low-latency shared-mode audio

## Language

C++20 or newer supported by the selected Visual Studio toolchain.

## Build system

CMake.

## GUI

Qt 6.

The GUI must remain separate from the real-time audio engine.

## Testing

GoogleTest.

## Profiling

Use:

- Visual Studio Profiler
- Windows Performance Analyzer
- Windows Performance Recorder

---

# 4. Repository Structure

```text
SyncWave/
│
├── CMakeLists.txt
├── README.md
├── LICENSE
├── .gitignore
│
├── docs/
│   ├── architecture.md
│   ├── synchronization.md
│   ├── experiments.md
│   └── decisions/
│       ├── 001-windows-wasapi.md
│       ├── 002-master-clock.md
│       └── 003-drift-correction.md
│
├── src/
│   ├── app/
│   │   └── main.cpp
│   │
│   ├── audio/
│   │   ├── AudioEngine.h
│   │   ├── AudioEngine.cpp
│   │   ├── AudioBuffer.h
│   │   ├── AudioFormat.h
│   │   ├── RingBuffer.h
│   │   └── RingBuffer.cpp
│   │
│   ├── windows/
│   │   ├── WasapiCapture.h
│   │   ├── WasapiCapture.cpp
│   │   ├── WasapiOutput.h
│   │   ├── WasapiOutput.cpp
│   │   ├── DeviceManager.h
│   │   ├── DeviceManager.cpp
│   │   ├── DeviceNotification.h
│   │   └── DeviceNotification.cpp
│   │
│   ├── sync/
│   │   ├── MasterClock.h
│   │   ├── MasterClock.cpp
│   │   ├── SyncController.h
│   │   ├── SyncController.cpp
│   │   ├── LatencyEstimator.h
│   │   ├── LatencyEstimator.cpp
│   │   ├── DriftEstimator.h
│   │   ├── DriftEstimator.cpp
│   │   ├── DelayBuffer.h
│   │   ├── DelayBuffer.cpp
│   │   ├── DriftCorrector.h
│   │   └── DriftCorrector.cpp
│   │
│   ├── dsp/
│   │   ├── Resampler.h
│   │   ├── Resampler.cpp
│   │   ├── ChannelMapper.h
│   │   ├── ChannelMapper.cpp
│   │   ├── FormatConverter.h
│   │   └── FormatConverter.cpp
│   │
│   ├── media/
│   │   ├── VlcController.h
│   │   └── VlcController.cpp
│   │
│   └── ui/
│       ├── MainWindow.*
│       ├── DeviceList.*
│       ├── Calibration.*
│       └── Diagnostics.*
│
├── tests/
│   ├── audio/
│   ├── sync/
│   ├── dsp/
│   └── integration/
│
└── experiments/
    ├── latency/
    ├── drift/
    └── av-sync/
```

---

# 5. High-Level Architecture

```text
                    ┌───────────────────────┐
                    │       Source          │
                    │ System / Application  │
                    └───────────┬───────────┘
                                │
                                v
                    ┌───────────────────────┐
                    │   WASAPI Loopback     │
                    │       Capture         │
                    └───────────┬───────────┘
                                │
                                v
                    ┌───────────────────────┐
                    │    Master Audio Bus   │
                    │   Canonical PCM Data  │
                    └───────────┬───────────┘
                                │
              ┌─────────────────┼─────────────────┐
              │                 │                 │
              v                 v                 v
       ┌────────────┐    ┌────────────┐    ┌────────────┐
       │ Output #1  │    │ Output #2  │    │ Output #3  │
       │ Delay      │    │ Delay      │    │ Delay      │
       └─────┬──────┘    └─────┬──────┘    └─────┬──────┘
             │                 │                 │
             v                 v                 v
       ┌────────────┐    ┌────────────┐    ┌────────────┐
       │  WASAPI    │    │  WASAPI    │    │  WASAPI    │
       │  Renderer  │    │  Renderer  │    │  Renderer  │
       └─────┬──────┘    └─────┬──────┘    └─────┬──────┘
             │                 │                 │
             v                 v                 v
         Device A          Device B          Device C
```

Timing/control plane:

```text
DeviceManager
      |
      v
LatencyEstimator
      |
      v
SyncController
      |
      +------> DelayBuffer
      |
      +------> MasterClock
      |
      +------> DriftEstimator
      |
      +------> DriftCorrector
```

---

# 6. Core Design Principle

Keep three systems conceptually separate.

## Audio data plane

Moves PCM samples.

```text
Capture -> RingBuffer -> DelayBuffer -> Renderer
```

## Timing plane

Determines when samples should be played.

```text
MasterClock
    |
Device clocks
    |
Latency measurements
    |
Drift estimation
```

## Control plane

Changes configuration.

```text
GUI
 |
SyncController
 |
DeviceManager
 |
Output instances
```

Do not build one giant `AudioManager` class containing everything.

---

# 7. Phase 0 — Development Environment

Install:

- Visual Studio 2022/compatible current Visual Studio
- Desktop C++ workload
- Windows SDK
- CMake
- Git
- Qt 6
- GoogleTest

Create the repository.

Initial build target:

```text
SyncWave.exe
```

At this stage it can simply print:

```text
SyncWave starting...
```

---

# 8. Phase 1 — Audio Device Enumeration

Implement:

```text
DeviceManager
```

Responsibilities:

- enumerate render endpoints
- identify active devices
- obtain endpoint IDs
- obtain friendly names
- obtain device state
- identify default output
- expose device list to the UI

Conceptually:

```cpp
struct AudioDevice {
    std::string id;
    std::string name;
    bool active;
};
```

The Windows Core Audio endpoint enumerator is the first major API dependency.

Do not implement Bluetooth pairing yourself.

Windows should manage Bluetooth pairing and connection. SyncWave only consumes the resulting audio endpoint.

---

# 9. Phase 2 — Hot-Plug Detection

Implement:

```text
IMMNotificationClient
```

Handle:

- device added
- device removed
- device state changed
- default device changed
- property changes

Expected behavior:

```text
Device connected
       |
       v
Notification
       |
       v
DeviceManager refresh
       |
       v
SyncWave updates UI
```

Later, if a currently playing device disappears:

```text
Device disconnected
       |
       v
Output renderer stops
       |
       v
SyncController removes endpoint
       |
       v
Remaining outputs continue
```

When it reconnects:

```text
Reconnect
   |
   v
Reinitialize WASAPI
   |
   v
Measure latency
   |
   v
Rejoin synchronization group
```

---

# 10. Phase 3 — Single WASAPI Renderer

Implement:

```text
WasapiOutput
```

Responsibilities:

1. Open endpoint.
2. Obtain mix format.
3. Create `IAudioClient`.
4. Initialize the render stream.
5. Obtain `IAudioRenderClient`.
6. Obtain the audio clock.
7. Feed PCM samples.
8. Handle buffer availability.
9. Stop cleanly.

First test:

Generate a sine wave.

```text
440 Hz sine
      |
      v
IAudioRenderClient
      |
      v
Headphones
```

Do not introduce synchronization yet.

The first success criterion is:

> SyncWave can reliably produce continuous audio through one selected Windows output device.

---

# 11. Phase 4 — Master Audio Buffer

Create a central audio bus.

```text
MasterAudioBus
```

It should contain canonical PCM audio.

A lock-free or carefully synchronized ring buffer should be used for real-time audio movement.

Example:

```text
Loopback Capture
      |
      v
+-------------------+
| Master RingBuffer |
+-------------------+
      |
      +----------+
      |          |
      v          v
 Output A     Output B
```

Important requirements:

- bounded memory
- predictable latency
- no uncontrolled allocations in the real-time path
- no expensive locks in the audio callback/render thread

---

# 12. Phase 5 — WASAPI Loopback Capture

Implement:

```text
WasapiCapture
```

The first source should be system audio.

Pipeline:

```text
Windows Audio Engine
        |
        v
WASAPI Loopback
        |
        v
PCM frames
        |
        v
Master RingBuffer
```

This means SyncWave does not need to understand whether the original source is:

- VLC
- Spotify
- YouTube
- a game
- a browser
- another application

For the MVP, SyncWave sees the resulting audio stream.

---

# 13. Phase 6 — Two Simultaneous Outputs

This is the first major milestone.

```text
Loopback
   |
   v
Master RingBuffer
   |
   +------------+
   |            |
   v            v
Output A     Output B
```

Both renderers receive the same logical audio timeline.

At this stage do not assume that the devices are synchronized.

Measure the difference.

The important research question becomes:

> If both outputs receive the same sample at the same logical time, how different are their physical playback times?

---

# 14. Phase 7 — Measure Before Fixing

This is one of the most important stages.

Do not immediately implement a “200 ms delay”.

First build an experiment.

Use a recognizable test signal:

```text
silence
  |
  v
short impulse
  |
  v
silence
```

Record the physical outputs with microphones or a suitable measurement setup.

For each device measure:

```text
T_device = time between reference event and physical acoustic event
```

Example:

```text
Wired headphones     18 ms
Bluetooth device A   82 ms
Bluetooth device B   119 ms
Bluetooth device C   147 ms
```

These are illustrative measurements, not guaranteed values.

The goal is to obtain actual measurements from your hardware.

---

# 15. Phase 8 — Per-Device Delay

Once latency is measurable, implement:

```text
DelayBuffer
```

Suppose:

```text
Device A = 20 ms
Device B = 80 ms
Device C = 140 ms
```

Choose:

```text
Target = maximum latency + safety margin
```

For example:

```text
Target = 160 ms
```

Then add:

```text
Device A delay = 140 ms
Device B delay = 80 ms
Device C delay = 20 ms
```

Conceptually:

```text
                 Target timeline
                       |
          +------------+------------+
          |            |            |
       Delay A      Delay B      Delay C
          |            |            |
       Device A     Device B     Device C
```

The important concept is:

> Do not make the fastest device faster. Delay the faster paths until they align with the slowest path.

---

# 16. Why Fixed 200 ms Is Not the Final Solution

A fixed 200 ms delay can be useful for a prototype.

It is not a robust production algorithm.

Different devices can have different latency.

Bluetooth latency can also vary because of:

- buffering
- radio conditions
- codec behavior
- OS scheduling
- power-management behavior
- device firmware

Therefore:

```text
Target latency
=
maximum measured device latency
+
jitter margin
+
safety margin
```

The exact margin should be experimentally determined.

---

# 17. Phase 9 — Automatic Latency Calibration

Implement:

```text
LatencyEstimator
```

Calibration procedure:

```text
1. Generate known reference signal.
2. Send identical signal to selected outputs.
3. Measure physical arrival.
4. Estimate latency for every endpoint.
5. Repeat multiple times.
6. Calculate robust estimate.
7. Store result.
8. Compute target latency.
9. Configure DelayBuffers.
```

Do not rely on a single measurement.

Use repeated measurements and examine:

- mean
- median
- variance
- min
- max
- outliers

A better estimator may use the median or another robust statistic.

---

# 18. Calibration Data Model

Example:

```cpp
struct DeviceTiming {
    double estimatedLatencyMs;
    double jitterMs;
    double addedDelayMs;
    double syncErrorMs;
};
```

A device could then appear as:

```text
Sony Buds
Latency:       83.4 ms
Jitter:         3.1 ms
Added delay:   78.6 ms
Sync error:     1.8 ms
```

These values should come from measurement, not hard-coded assumptions.

---

# 19. Phase 10 — Master Clock

Audio samples alone are not enough.

Every output device has its own timing behavior.

Create:

```text
MasterClock
```

The master clock defines the logical SyncWave timeline.

Possible reference:

```text
high-resolution monotonic system clock
```

Then correlate:

```text
MasterClock
     |
     +---- Device A clock
     |
     +---- Device B clock
     |
     +---- Device C clock
```

The purpose is to detect long-term divergence.

---

# 20. Phase 11 — Device Clock Measurement

Use WASAPI audio-clock functionality to observe stream/device position and data rate.

For each output:

```text
clock position
sample rate
frames rendered
timestamp
```

Record samples periodically.

Example:

```text
Time       Device A      Device B
-----------------------------------
0 s        0             0
10 s       480000        479995
20 s       960000        959989
30 s       1440000       1439984
```

The difference can reveal clock drift.

---

# 21. Clock Drift

Two devices can both claim:

```text
48,000 Hz
```

but their physical clocks may not be exactly identical.

For example:

```text
Device A = 48,000.00 Hz
Device B = 47,999.85 Hz
```

The difference may look tiny.

Over a long enough period, it accumulates.

Therefore:

```text
Latency correction
```

solves an initial offset.

But:

```text
Drift correction
```

is required for long-duration synchronization.

---

# 22. Phase 12 — Drift Estimator

Implement:

```text
DriftEstimator
```

It should estimate:

```text
offset(t)
```

and:

```text
drift_rate
```

Conceptually:

```text
device_position - master_position
```

over time.

Fit a trend rather than reacting aggressively to every small measurement.

The estimator should distinguish:

- measurement noise
- normal jitter
- real clock drift
- sudden device reset/reconnect

---

# 23. Phase 13 — Drift Correction

Possible strategies:

## Strategy A — Audio clock adjustment

Where supported and appropriate, use Windows audio clock adjustment facilities.

This should be treated as a control-plane operation, not something performed carelessly inside the real-time audio callback.

## Strategy B — Adaptive resampling

Slightly change the effective sample rate of an output stream.

Example:

```text
nominal = 48,000 Hz

correction:
47,999.7 Hz
or
48,000.3 Hz
```

The correction should be extremely small.

The listener should not perceive pitch changes.

## Strategy C — Tiny controlled buffer correction

For small errors, the system may make extremely small timing adjustments.

Avoid crude periodic frame dropping/insertion because it can create audible artifacts.

---

# 24. Phase 14 — DSP Layer

Different devices may expose different formats.

Implement:

```text
FormatConverter
ChannelMapper
Resampler
```

Example:

```text
Master PCM
   |
   +--> 48 kHz / Stereo
   |
   +--> 44.1 kHz / Stereo
   |
   +--> 48 kHz / Mono
```

The master representation should be stable.

Per-device conversion happens as close as possible to the output boundary.

---

# 25. Real-Time Audio Rules

The audio thread is special.

Avoid in the real-time path:

- dynamic memory allocation
- blocking mutexes
- file I/O
- logging to disk
- network operations
- expensive UI calls
- unpredictable system calls

Prefer:

- preallocated buffers
- ring buffers
- lock-free structures where justified
- atomic state
- predictable DSP

The GUI must never block the audio engine.

---

# 26. Phase 15 — Synchronization Controller

Create:

```text
SyncController
```

Responsibilities:

```text
Device timing
     |
LatencyEstimator
     |
DriftEstimator
     |
     v
SyncController
     |
     +--> target latency
     +--> per-device delay
     +--> drift correction
     +--> synchronization state
```

Possible states:

```text
DISCONNECTED
DISCOVERING
CALIBRATING
SYNCING
LOCKED
DRIFT_CORRECTING
ERROR
```

Example:

```text
CALIBRATING
     |
     v
SYNCING
     |
     v
LOCKED
     |
     v
DRIFT_CORRECTING
```

---

# 27. Synchronization Algorithm

Initial version:

```text
for every device:

    measure latency

targetLatency =
    max(deviceLatency)
    + safetyMargin

for every device:

    addedDelay =
        targetLatency
        - measuredLatency

apply addedDelay
```

Continuous version:

```text
while running:

    update device clocks

    estimate offset

    estimate drift

    if offset is small:
        keep state

    if drift is detected:
        apply tiny correction

    if synchronization becomes unstable:
        increase buffer margin or recalibrate
```

Do not continuously jump the delay buffer.

Corrections should be smooth.

---

# 28. Phase 16 — Diagnostics

Build a diagnostics subsystem early.

Show:

```text
Device
Latency
Jitter
Added Delay
Clock Rate
Clock Offset
Sync Error
Buffer Level
Dropouts
```

Example:

```text
SYNCWAVE DIAGNOSTICS

Device                  Latency   Delay   Error
------------------------------------------------
Wired Headphones         18 ms    143 ms   1.1 ms
Bluetooth Buds A         84 ms     77 ms   1.6 ms
Bluetooth Speaker       151 ms     10 ms   2.0 ms

Target latency: 161 ms
Master sample rate: 48000 Hz
System load: 7.4%
```

This makes the project scientifically testable.

---

# 29. Phase 17 — Device Failure Recovery

Test:

```text
Bluetooth device disconnected
```

Expected:

```text
Device disappears
      |
      v
Notification
      |
      v
Output marked unavailable
      |
      v
Other devices continue
```

Then:

```text
Bluetooth reconnects
      |
      v
Endpoint appears
      |
      v
Reinitialize renderer
      |
      v
Recalibrate
      |
      v
Rejoin synchronization
```

Do not crash the whole audio engine because one endpoint disappears.

---

# 30. Phase 18 — GUI

Only after the audio engine is stable should the GUI become sophisticated.

Basic UI:

```text
SYNCWAVE

Audio Source
[ System Audio ]

Output Devices

[x] Wired Headphones
    18 ms

[x] Bluetooth Buds
    84 ms

[x] Bluetooth Speaker
    151 ms

Target Latency
[ 161 ms ]

[ Auto Calibrate ]

[ Start ]
[ Stop ]
```

Advanced diagnostics:

```text
Sync Accuracy
Clock Drift
Buffer Health
Latency History
Device State
```

---

# 31. Phase 19 — VLC Integration

Do not start with Netflix or DRM-protected applications.

Use VLC first because it provides a controllable media environment.

The goal is eventually:

```text
VLC
 |
 | video clock
 v
SyncWave A/V Controller
 |
 +---- video timing
 |
 +---- master audio timing
          |
          v
     SyncWave engine
          |
     multiple outputs
```

The first VLC milestone should be:

> Play a known test video and determine whether SyncWave's additional audio latency causes visible lip-sync offset.

---

# 32. Audio/Video Synchronization

This is a separate problem from device-to-device synchronization.

There are three timing layers:

```text
Layer 1:
Device A <-> Device B

Layer 2:
Audio clock <-> master clock

Layer 3:
Audio <-> Video
```

A system can be perfect at Layer 1 and still fail at Layer 3.

Example:

```text
Video frame:
1000 ms

Audio reaches listener:
1170 ms
```

Result:

```text
Audio is 170 ms late.
```

Therefore the final system needs an A/V controller.

---

# 33. Recommended A/V Architecture

```text
                Media Player
               /            \
              v              v
          Video Clock     Audio Stream
              |              |
              v              v
        A/V Controller    SyncWave
              |              |
              +-------+------+
                      |
                      v
               synchronized
                  playback
```

For VLC, the first implementation can use the player's audio-delay/timing controls rather than modifying the video itself.

---

# 34. Acoustic Calibration — Advanced Research

Electrical/software latency is not necessarily the final physical latency.

For speakers in a room:

```text
Speaker
   |
   | air propagation
   v
Microphone
```

For future versions, use a microphone to measure actual acoustic arrival time.

Procedure:

```text
SyncWave sends impulse
       |
       +--> Device A
       |
       +--> Device B
       |
       v
Microphone records result
       |
       v
Cross-correlation
       |
       v
Arrival-time estimate
```

This could eventually compensate for:

- device latency
- room distance
- speaker processing delay

For earbuds, physical acoustic distance is usually much less important than device/transport latency.

---

# 35. Experimental Methodology

Every major synchronization change should have an experiment.

Record:

```text
Experiment ID
Date
Hardware
OS
Sample rate
Buffer size
Number of devices
Measured latency
Jitter
Drift
Sync error
CPU usage
```

Example:

```text
Experiment: LAT-001

Devices:
- Wired headset
- Bluetooth earbuds
- Bluetooth speaker

Duration:
60 minutes

Initial max error:
XX ms

Final max error:
XX ms

Mean error:
XX ms

Drift:
XX ms/min

Dropouts:
X
```

Use real measurements rather than marketing claims.

---

# 36. Benchmark Metrics

Track:

## Initial offset

Difference immediately after synchronization.

## Mean synchronization error

Average physical timing difference.

## Maximum synchronization error

Worst observed timing difference.

## Jitter

Short-term timing variation.

## Drift

Long-term divergence.

## Dropout rate

Number of audio interruptions.

## Recovery time

Time to recover after disconnect/reconnect.

## CPU usage

Real-time processing cost.

## Memory usage

Buffer and application memory.

---

# 37. Test Matrix

Test combinations such as:

```text
1 wired
1 Bluetooth

2 wired

2 Bluetooth

1 wired + 1 Bluetooth

1 wired + 2 Bluetooth

1 wired + 3 Bluetooth

USB + wired + Bluetooth

different sample rates

device reconnect

device disconnect

sleep/wake

long-duration playback
```

Long-duration tests are especially important.

Run:

```text
10 min
30 min
60 min
2 hr
```

The goal is to determine whether devices slowly diverge.

---

# 38. Version Roadmap

## v0.1

Device enumeration.

## v0.2

Multiple WASAPI outputs.

## v0.3

Master audio bus and per-device delay.

## v0.4

Automatic latency measurement.

## v0.5

Master clock and device clock monitoring.

## v0.6

Drift estimation and correction.

## v0.7

Reconnect/failure recovery.

## v0.8

Format conversion and robust DSP pipeline.

## v0.9

VLC integration.

## v0.10

A/V synchronization.

## v1.0

Polished UI, automatic calibration, diagnostics, benchmarks and documentation.

---

# 39. First Seven Days

## Day 1

Set up:

- Visual Studio
- Windows SDK
- CMake
- Qt
- Git

Create:

```text
SyncWave/
CMakeLists.txt
src/
tests/
docs/
```

Get an executable building.

## Day 2

Implement:

```text
DeviceManager
```

Successfully list all render devices.

## Day 3

Implement:

```text
IMMNotificationClient
```

Plug/unplug devices and verify that SyncWave receives notifications.

## Day 4

Implement:

```text
WasapiOutput
```

Generate a 440 Hz test tone.

## Day 5

Implement:

```text
WasapiCapture
```

Capture system audio through WASAPI loopback.

## Day 6

Implement:

```text
MasterRingBuffer
```

Connect:

```text
Loopback -> RingBuffer -> Renderer
```

## Day 7

Create two renderers:

```text
Master RingBuffer
       |
       +---- Output A
       |
       +---- Output B
```

At the end of Day 7, the goal is:

> One source playing simultaneously through two independent Windows audio endpoints.

No automatic synchronization yet.

---

# 40. First Major Research Question

Once two outputs work, stop and measure.

The first serious experiment is:

> How far apart are two devices physically playing the same logical audio sample?

This gives the project a measurable research foundation.

Do not assume:

```text
Bluetooth = X ms
```

or:

```text
wired = Y ms
```

Measure your actual hardware.

---

# 41. What Not To Build First

Do NOT begin with:

- Bluetooth protocol implementation
- custom Bluetooth pairing
- Netflix integration
- DRM interception
- microphone room calibration
- complex AI
- fancy UI
- mobile application
- cloud backend

These distract from the core synchronization problem.

The first objective is:

```text
Windows C++
    |
WASAPI loopback
    |
Master RingBuffer
    |
Multiple WASAPI renderers
    |
Measured synchronization
```

---

# 42. Core Technical Risks

## Risk 1 — Bluetooth latency variability

A Bluetooth endpoint may not have a perfectly constant delay.

Mitigation:

- measure repeatedly
- use safety margins
- monitor continuously
- support adaptive correction

## Risk 2 — Independent clocks

Two endpoints can drift apart.

Mitigation:

- monitor audio clocks
- estimate drift
- use controlled resampling/clock correction

## Risk 3 — Buffer underruns

Real-time output can fail if buffers are not managed correctly.

Mitigation:

- sufficient buffering
- event-driven rendering
- careful thread priorities
- diagnostics

## Risk 4 — Device reconnect

Bluetooth devices can disappear.

Mitigation:

- endpoint notifications
- renderer lifecycle management
- automatic reinitialization

## Risk 5 — Lip-sync

Synchronizing outputs does not automatically synchronize audio with video.

Mitigation:

- dedicated A/V synchronization layer
- start with VLC
- measure actual A/V offset

---

# 43. Performance Architecture

Suggested threads:

```text
GUI Thread
    |
    +-------------------------------+
                                    |
Capture Thread                  Control Thread
    |                               |
    v                               v
Master RingBuffer             SyncController
    |                               |
    +-------------------------------+
                    |
          +---------+---------+
          |         |         |
          v         v         v
      Render A  Render B  Render C
```

Avoid making the GUI thread responsible for audio timing.

---

# 44. Synchronization State Machine

```text
             +--------------+
             | DISCONNECTED |
             +------+-------+
                    |
                    v
             +--------------+
             | DISCOVERING  |
             +------+-------+
                    |
                    v
             +--------------+
             | CALIBRATING  |
             +------+-------+
                    |
                    v
             +--------------+
             |    SYNCING   |
             +------+-------+
                    |
                    v
             +--------------+
             |    LOCKED    |
             +------+-------+
                    |
                    v
             +--------------+
             | DRIFT FIXING |
             +------+-------+
                    |
                    +-------> LOCKED
```

If an endpoint fails:

```text
LOCKED
  |
  v
DEVICE FAILURE
  |
  v
REINITIALIZE
  |
  v
CALIBRATING
```

---

# 45. Definition of Done for v1.0

SyncWave v1.0 should be able to:

- enumerate Windows audio endpoints
- detect endpoint changes
- capture system audio
- render to multiple endpoints
- automatically estimate output latency
- synchronize outputs using measured latency
- monitor synchronization continuously
- detect clock drift
- compensate for small drift
- handle device reconnects
- convert common audio formats
- provide useful diagnostics
- maintain synchronization during long playback
- integrate with VLC for controlled A/V synchronization
- provide reproducible benchmark results

---

# 46. Final Product Concept

The final system should feel like:

```text
                         SYNCWAVE
                            |
             +--------------+--------------+
             |                             |
          SOURCE                        CLOCK
             |                             |
             v                             v
       System/VLC Audio             Master Timeline
             |                             |
             +-------------+---------------+
                           |
                           v
                    Master Audio Bus
                           |
             +-------------+-------------+
             |             |             |
             v             v             v
          Device A      Device B      Device C
             |             |             |
        delay/DRIFT     delay/DRIFT   delay/DRIFT
             |             |             |
             +-------------+-------------+
                           |
                           v
                    Physical Playback
                           |
                           v
                 synchronized audio
```

The central intellectual contribution is not “play audio on several devices.”

It is:

> Maintain a common audio timeline across independent heterogeneous playback devices despite different latency, jitter, buffering and clock behavior.

That is the problem SyncWave should be designed around.

---

# 47. Immediate Next Step

Do not implement the entire architecture at once.

Start with exactly this chain:

```text
C++ / CMake
      |
      v
DeviceManager
      |
      v
WASAPI Loopback Capture
      |
      v
Master RingBuffer
      |
      v
WASAPI Output #1
      |
      +---- WASAPI Output #2
```

Once this works reliably, begin the latency experiment.

Then build:

```text
LatencyEstimator
      |
      v
DelayBuffer
      |
      v
SyncController
```

Only after stable initial synchronization should you implement:

```text
MasterClock
      |
      v
DriftEstimator
      |
      v
DriftCorrector
```

And only after the audio synchronization engine is reliable should you move to:

```text
VLC
 |
A/V Sync
 |
SyncWave
```

This order minimizes complexity while ensuring every major subsystem is experimentally validated before the next one is introduced.
