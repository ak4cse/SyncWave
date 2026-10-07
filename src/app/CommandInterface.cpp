#include "CommandInterface.h"
#include "../windows/DeviceManager.h"
#include "../audio/AudioEngine.h"
#include "../sync/AcousticCalibrator.h"
#include "../sync/CalibrationStore.h"
#include "../av/VlcMediaEngine.h"

#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <chrono>
#include <thread>

namespace syncwave {

constexpr const char* SYNCWAVE_VERSION = "0.13.0";

static std::atomic<bool> g_stopRequested{false};

static BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
        g_stopRequested.store(true);
        return TRUE;
    }
    return FALSE;
}

static std::vector<std::string> splitString(const std::string& str, char delim) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (!item.empty()) {
            tokens.push_back(item);
        }
    }
    return tokens;
}

static std::vector<double> parseDoubleList(const std::string& str) {
    auto tokens = splitString(str, ',');
    std::vector<double> vals;
    vals.reserve(tokens.size());
    for (const auto& t : tokens) {
        try {
            vals.push_back(std::stod(t));
        } catch (...) {
            vals.push_back(0.0);
        }
    }
    return vals;
}

CommandInterface::CommandInterface()
    : deviceManager_(std::make_unique<DeviceManager>()),
      audioEngine_(std::make_unique<AudioEngine>()) {}

CommandInterface::~CommandInterface() = default;

void CommandInterface::printVersion() const {
    std::cout << "SyncWave v" << SYNCWAVE_VERSION << " (Windows CLI Audio Engine)\n";
}

void CommandInterface::printHelp() const {
    printVersion();
    std::cout << "\nUsage:\n"
              << "  syncwave devices [--all]                   Enumerate audio output devices\n"
              << "  syncwave watch [--timeout <sec>]           Monitor audio endpoint changes in real time\n"
              << "  syncwave tone [options]                    Play synthetic PCM sine wave through Master Audio Bus\n"
              << "  syncwave capture [options]                 Capture Windows system audio via WASAPI Loopback and route to outputs\n"
              << "  syncwave calibrate [options]               Measure software latencies and compute alignment delay plan\n"
              << "  syncwave clock-test [options]              Run high-precision 60s clock drift and stability experiment\n"
              << "  syncwave latency-test [options]            Run deterministic transient pulse test to evaluate software latency\n"
              << "  syncwave acoustic-calibrate [options]      Physically calibrate end-to-end acoustic arrival offset using chirp/mic\n"
              << "  syncwave acoustic-verify [options]         Verify two-device acoustic alignment before and after compensation\n"
              << "  syncwave stress [options]                  Run long-duration multi-device synchronization stress test (M12)\n"
              << "  syncwave media <file> [options]            Play media file/URL via VLC synchronized through multi-output engine (M13)\n"
              << "  syncwave status                            Display engine, router, and master audio bus status\n"
              << "  syncwave help                              Show this help message\n"
              << "  syncwave --version                         Display version\n\n"
              << "Options for 'media':\n"
              << "  --outputs, -O <id1,id2,...>                Target output endpoints (default: first two active endpoints)\n"
              << "  --sync <none|software|adaptive>            Alignment mode (default: adaptive drift correction)\n"
              << "  --delay <ms1,ms2,...>                      Per-output delay in ms\n"
              << "  --offsets <ms1,ms2,...>                    Optional manual physical calibration offsets in ms\n"
              << "  --duration, -t <sec>                       Playback duration limit in seconds (0 = full file)\n"
              << "  --csv, --log <path>                        Telemetry CSV log path (default: media_av_telemetry.csv)\n\n"
              << "Options for 'capture':\n"
              << "  --source, -s <index|id>                    Capture endpoint (default: system default)\n"
              << "  --output, -o <index|id>                    Output endpoint (can be repeated for multiple outputs)\n"
              << "  --outputs, -O <id1,id2,...>                Comma-separated list of output endpoints\n"
              << "  --delay <ms1,ms2,...|ms>                   Per-output or default delay in milliseconds\n"
              << "  --sync <none|software|adaptive>            Alignment mode (none, software, or adaptive drift correction)\n"
              << "  --offsets <ms1,ms2,...>                    Optional manual physical calibration offsets in ms\n"
              << "  --duration, -t <sec>                       Duration in seconds (0 = continuous, default: 5s)\n\n"
              << "Options for 'tone':\n"
              << "  --device, -d, -o <index|id>                Target output endpoint (can be repeated for multiple outputs)\n"
              << "  --outputs, -O <id1,id2,...>                Comma-separated list of target output endpoints\n"
              << "  --frequency, -f <Hz>                       Tone frequency in Hz (default: 440 Hz)\n"
              << "  --delay <ms1,ms2,...|ms>                   Per-output or default delay in milliseconds\n"
              << "  --sync <none|software|adaptive>            Alignment mode (none, software, or adaptive drift correction)\n"
              << "  --offsets <ms1,ms2,...>                    Optional manual physical calibration offsets in ms\n"
              << "  --duration, -t <sec>                       Playback duration in seconds (0 = continuous, default: 5s)\n"
              << "  --volume, -v <0.0..1.0>                    Volume amplitude level (default: 0.25)\n\n"
              << "Options for 'calibrate':\n"
              << "  --outputs, -O <id1,id2,...>                Target output endpoints (default: first two active endpoints)\n"
              << "  --offsets <ms1,ms2,...>                    Optional manual physical calibration offsets in ms\n\n"
              << "Options for 'clock-test':\n"
              << "  --outputs, -O <id1,id2,...>                Target output endpoints (default: first two active endpoints)\n"
              << "  --duration, -t <sec>                       Duration in seconds (default: 60s)\n\n"
              << "Options for 'latency-test':\n"
              << "  --outputs, -O <id1,id2,...>                Target output endpoints (default: first two active endpoints)\n"
              << "  --delay <ms1,ms2,...|ms>                   Per-output or default delay in milliseconds\n"
              << "  --sync <none|software|adaptive>            Alignment mode (none, software, or adaptive drift correction)\n"
              << "  --offsets <ms1,ms2,...>                    Optional manual physical calibration offsets in ms\n"
              << "  --runs, -n <count>                         Number of repeated test runs (default: 5)\n\n"
              << "Options for 'acoustic-calibrate':\n"
              << "  --device, -d <index|id>                    Output endpoint under test (default: system default render device)\n"
              << "  --microphone, -m <index|id>                Microphone capture endpoint (default: system default microphone)\n"
              << "  --runs, -r <count>                         Number of calibration runs (default: 5)\n"
              << "  --chirp-duration <sec>                     Chirp duration in seconds (default: 0.150s)\n"
              << "  --volume, -v <0.05..0.50>                  Chirp amplitude volume (default: 0.25)\n"
              << "  --save / --no-save                         Persist calibration for future multi-device sync (default: save)\n\n"
              << "Options for 'acoustic-verify':\n"
              << "  --outputs, -O <idA,idB>                    Two target output endpoints to verify\n"
              << "  --microphone, -m <index|id>                Reference microphone endpoint\n"
              << "  --delay <ms>                               Intentional compensation delay applied to Output A in Exp B (default: 24.28 ms)\n"
              << "  --runs, -r <count>                         Number of test runs per device per experiment (default: 5)\n"
              << "  --chirp-duration <sec>                     Chirp duration in seconds (default: 0.150s)\n"
              << "  --volume, -v <0.05..0.50>                  Chirp amplitude volume (default: 0.25)\n\n"
              << "Options for 'stress':\n"
              << "  --outputs, -O <id1,id2,...>                Target output endpoints (default: first two active devices)\n"
              << "  --duration, -t <sec>                       Duration in seconds (default: 1800s / 30m; 3600s / 60m)\n"
              << "  --sync <none|software|adaptive>            Alignment mode (default: adaptive drift correction)\n"
              << "  --csv, --log <path>                        Machine-readable 1-second telemetry CSV log (default: stress_telemetry.csv)\n"
              << "  --offsets <ms1,ms2,...>                    Optional manual physical calibration offsets in ms\n"
              << "  --delay <ms1,ms2,...>                      Optional per-output manual delays in ms\n"
              << "  --disconnect-test <sec>                    Simulate/trigger endpoint disconnect at specified second and reconnect after 5s\n"
              << "  --volume, -v <0.0..1.0>                    Tone volume (default: 0.25; use 0.0 for quiet testing)\n\n"
              << "Options for 'calibration':\n"
              << "  syncwave calibration list                  List all persisted per-device acoustic calibrations\n"
              << "  syncwave calibration clear [--device <id>] Clear stored calibration for a device or all devices\n\n";
}

int CommandInterface::handleDevicesCommand(const std::vector<std::string>& args) {
    bool showAll = false;
    for (const auto& arg : args) {
        if (arg == "--all" || arg == "-a") {
            showAll = true;
        }
    }

    try {
        auto devices = deviceManager_->enumerateDevices(!showAll);

        std::cout << "\nSyncWave Audio Devices\n";
        std::cout << "======================\n\n";

        if (devices.empty()) {
            std::cout << "No audio render devices found.\n\n";
            return 0;
        }

        for (size_t i = 0; i < devices.size(); ++i) {
            const auto& dev = devices[i];
            std::cout << "[" << i << "] " << dev.name;
            if (dev.isDefault) {
                std::cout << " [DEFAULT]";
            }
            std::cout << "\n";
            std::cout << "    State: " << dev.stateString() << "\n";
            std::cout << "    ID:    " << dev.id << "\n\n";
        }

        std::cout << "Total endpoints: " << devices.size() << "\n\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error during device enumeration: " << ex.what() << "\n";
        return 1;
    }
}

int CommandInterface::handleWatchCommand(const std::vector<std::string>& args) {
    int timeoutSec = -1;
    for (size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "--timeout" || args[i] == "-t") && i + 1 < args.size()) {
            try {
                timeoutSec = std::stoi(args[i + 1]);
            } catch (...) {}
            break;
        }
    }

    std::cout << "\nSyncWave Device Watcher\n";
    std::cout << "=======================\n";
    std::cout << "Monitoring Windows audio endpoint changes in real time...\n";
    if (timeoutSec > 0) {
        std::cout << "Will automatically exit after " << timeoutSec << " second(s).\n";
    }
    std::cout << "Press Ctrl+C to stop.\n\n";

    g_stopRequested.store(false);
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::queue<DeviceEvent> eventQueue;

    bool started = deviceManager_->startMonitoring([&](const DeviceEvent& ev) {
        std::lock_guard<std::mutex> lock(queueMutex);
        eventQueue.push(ev);
        queueCv.notify_one();
    });

    if (!started) {
        std::cerr << "Failed to register endpoint notification client.\n";
        SetConsoleCtrlHandler(consoleCtrlHandler, FALSE);
        return 1;
    }

    auto startTime = std::chrono::steady_clock::now();

    while (!g_stopRequested.load()) {
        if (timeoutSec > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed >= timeoutSec) {
                break;
            }
        }

        std::unique_lock<std::mutex> lock(queueMutex);
        if (queueCv.wait_for(lock, std::chrono::milliseconds(150), [&]() {
            return !eventQueue.empty() || g_stopRequested.load();
        })) {
            while (!eventQueue.empty()) {
                DeviceEvent ev = eventQueue.front();
                eventQueue.pop();

                lock.unlock();

                switch (ev.type) {
                    case DeviceEventType::Added:
                        std::cout << "[DEVICE ADDED]\n";
                        std::cout << "  Name:  " << (ev.device ? ev.device->name : "Unknown Audio Device") << "\n";
                        std::cout << "  State: " << (ev.device ? ev.device->stateString() : deviceStateToString(ev.newState)) << "\n";
                        std::cout << "  ID:    " << ev.deviceId << "\n\n";
                        break;

                    case DeviceEventType::Removed:
                        std::cout << "[DEVICE REMOVED]\n";
                        std::cout << "  ID:    " << ev.deviceId << "\n\n";
                        break;

                    case DeviceEventType::StateChanged:
                        std::cout << "[DEVICE STATE CHANGED]\n";
                        std::cout << "  Name:  " << (ev.device ? ev.device->name : "Unknown Audio Device") << "\n";
                        std::cout << "  State: " << deviceStateToString(ev.newState) << "\n";
                        std::cout << "  ID:    " << ev.deviceId << "\n\n";
                        break;

                    case DeviceEventType::DefaultChanged:
                        std::cout << "[DEFAULT DEVICE CHANGED]\n";
                        std::cout << "  New default: " << (ev.device ? ev.device->name : "(None)") << "\n";
                        if (!ev.details.empty()) {
                            std::cout << "  " << ev.details << "\n";
                        }
                        std::cout << "  ID:          " << ev.deviceId << "\n\n";
                        break;

                    case DeviceEventType::PropertyChanged:
                        std::cout << "[DEVICE PROPERTY CHANGED]\n";
                        std::cout << "  Name:     " << (ev.device ? ev.device->name : "Unknown Audio Device") << "\n";
                        std::cout << "  Property: " << ev.details << "\n";
                        std::cout << "  ID:       " << ev.deviceId << "\n\n";
                        break;
                }
                std::cout << std::flush;

                lock.lock();
            }
        }
    }

    std::cout << "\nStopping device monitor...\n" << std::flush;
    deviceManager_->stopMonitoring();
    SetConsoleCtrlHandler(consoleCtrlHandler, FALSE);
    std::cout << "Monitoring stopped cleanly.\n\n" << std::flush;

    return 0;
}

static void printOutputTelemetry(const std::vector<DeviceOutputTelemetry>& outputs,
                                 const std::vector<PairwiseDriftEstimate>& pairwiseDrift) {
    std::cout << "Per-Output Timing & Audio Telemetry (" << outputs.size() << " endpoints):\n";
    if (outputs.empty()) {
        std::cout << "  (No outputs configured)\n\n";
        return;
    }

    for (size_t i = 0; i < outputs.size(); ++i) {
        const auto& out = outputs[i];
        std::cout << "  [" << i << "] " << out.deviceName << "\n";
        std::cout << "      State:            " << outputStateToString(out.state) << "\n";
        std::cout << "      Available:        " << (out.isAvailable ? "Yes" : "No (Disconnected)") << "\n";
        std::cout << "      Format:           " << out.format.formatString() << "\n";
        std::cout << "      Queue:            " << out.queueAvailable << " / " << out.queueCapacity << " frames\n";
        std::cout << "      Frames routed:    " << out.framesRouted << "\n";
        std::cout << "      Frames consumed:  " << out.framesConsumed << "\n";
        if (out.format.sampleRate != out.masterSampleRate) {
            std::cout << "      Frames resampled: " << out.framesResampled << " (" 
                      << out.masterSampleRate << " -> " << out.format.sampleRate << " Hz)\n";
        }
        std::cout << "      WASAPI submitted: " << out.framesSubmitted << "\n";
        std::cout << "      Queue underruns:  " << out.queueUnderruns << "\n";
        std::cout << "      Queue overruns:   " << out.queueOverruns << "\n";
        std::cout << "      WASAPI underruns: " << out.wasapiUnderruns << "\n";
        if (out.streamLatencyMs > 0.0) {
            std::cout << "      Stream latency:   " << out.streamLatencyMs << " ms\n";
        } else if (out.streamLatencyMs == 0.0 && out.clockSampleCount > 0) {
            std::cout << "      Stream latency:   0.0 ms (event-driven shared mode)\n";
        } else {
            std::cout << "      Stream latency:   unavailable\n";
        }
        double paddingMs = (out.format.sampleRate > 0) ? (static_cast<double>(out.currentPadding) * 1000.0 / out.format.sampleRate) : 0.0;
        std::cout << "      Current padding:  " << out.currentPadding << " frames (" << paddingMs << " ms)\n";
        std::cout << "      Clock frequency:  " << out.clockFrequency << " Hz\n";
        std::cout << "      Clock position:   " << out.clockPosition << " frames\n";
        if (out.estimatedClockRateHz > 0.0) {
            std::cout << "      Estimated rate:   " << out.estimatedClockRateHz << " Hz\n";
            std::cout << "      Nominal error:    " << (out.rateErrorPpm >= 0.0 ? "+" : "") << out.rateErrorPpm << " ppm\n";
            std::cout << "      Measurement span: " << out.measurementDurationSec << " s (" << out.clockSampleCount << " samples)\n";
        } else {
            std::cout << "      Estimated rate:   accumulating (" << out.clockSampleCount << " samples)\n";
        }
        std::cout << "      Sync state:       " << syncStateToString(out.syncState) << "\n";
        std::cout << "      Configured delay: " << out.latencyModel.configuredDelayMs << " ms ("
                  << out.latencyModel.appliedDelayFrames << " frames)\n";
        if (out.latencyModel.optionalCalibrationOffsetMs != 0.0) {
            std::cout << "      Calibration off:  " << (out.latencyModel.optionalCalibrationOffsetMs >= 0.0 ? "+" : "")
                      << out.latencyModel.optionalCalibrationOffsetMs << " ms\n";
        }
        std::cout << "      Est. SW latency:  " << out.latencyModel.estimatedSoftwareLatencyMs << " ms\n";
        std::cout << "      Effective latency:" << out.latencyModel.effectiveLatencyMs << " ms\n";
        if (out.driftState != DriftCorrectionState::Disabled) {
            std::cout << "      Drift state:      " << driftCorrectionStateToString(out.driftState) << "\n";
            std::cout << "      Phase error:      " << (out.phaseErrorMs >= 0.0 ? "+" : "") << out.phaseErrorMs << " ms"
                      << " (filtered: " << (out.filteredPhaseErrorMs >= 0.0 ? "+" : "") << out.filteredPhaseErrorMs << " ms)\n";
            std::cout << "      Drift error:      " << (out.rawDriftPpm >= 0.0 ? "+" : "") << out.rawDriftPpm << " ppm"
                      << " (filtered: " << (out.filteredDriftPpm >= 0.0 ? "+" : "") << out.filteredDriftPpm << " ppm)\n";
            std::cout << "      Drift confidence: " << std::fixed << std::setprecision(1) << (out.driftConfidence * 100.0) << "%\n";
            std::cout << "      Target rate adj:  " << (out.targetRateAdjustmentPpm >= 0.0 ? "+" : "") << out.targetRateAdjustmentPpm << " ppm\n";
            std::cout << "      Active rate adj:  " << (out.currentRateAdjustmentPpm >= 0.0 ? "+" : "") << out.currentRateAdjustmentPpm << " ppm\n";
        }
        std::cout << "\n";
    }

    if (!pairwiseDrift.empty()) {
        std::cout << "Relative Clock Drift Estimates (" << pairwiseDrift.size() << " pair" << (pairwiseDrift.size() > 1 ? "s" : "") << "):\n";
        for (const auto& pair : pairwiseDrift) {
            std::cout << "  " << pair.deviceNameA << " <-> " << pair.deviceNameB << ":\n";
            if (pair.isValid) {
                std::cout << "      Relative drift:   " << (pair.relativeDriftPpm >= 0.0 ? "+" : "") << pair.relativeDriftPpm << " ppm\n";
                std::cout << "      Rate ratio:       " << pair.rateRatio << " (A / B normalized)\n";
                std::cout << "      Estimated rates:  " << pair.estimatedRateA << " Hz vs " << pair.estimatedRateB << " Hz\n";
                std::cout << "      Relative offset:  " << (pair.relativeOffsetSec * 1000.0) << " ms\n";
                std::cout << "      Measurement span: " << pair.measurementDurationSec << " s\n";
            } else {
                std::cout << "      Status:           accumulating timing samples...\n";
            }
            std::cout << "\n";
        }
    }

    std::cout << "[TIMING NOTE] Software timestamps and WASAPI stream latency do NOT measure physical acoustic latency.\n"
              << "Hardware DAC filtering, Bluetooth A2DP transport buffering, and speaker drivers introduce additional delay.\n"
              << "Delay alignment and acoustic synchronization will be implemented in Milestones 8+.\n\n";
}

int CommandInterface::handleStatusCommand(const std::vector<std::string>& /*args*/) {
    auto diag = audioEngine_->getDiagnostics();

    std::cout << "\nSYNCWAVE STATUS\n";
    std::cout << "===============\n\n";

    std::cout << "Engine:\n";
    std::cout << "  State: " << (diag.isRunning ? "RUNNING" : "STOPPED") << "\n";
    std::cout << "  Mode:  " << (diag.isCaptureMode ? "WASAPI Loopback Capture" : "Synthetic Tone") << "\n\n";

    if (diag.isCaptureMode && !diag.captureDeviceName.empty()) {
        std::cout << "Capture Source:\n";
        std::cout << "  Device:          " << diag.captureDeviceName << "\n";
        std::cout << "  Format:          " << diag.captureFormat.formatString() << "\n";
        std::cout << "  Frames captured: " << diag.framesCaptured << "\n";
        std::cout << "  Packets captured:" << diag.packetsCaptured << "\n";
        std::cout << "  Silence packets: " << diag.silencePackets << "\n";
        std::cout << "  Discontinuities: " << diag.discontinuities << "\n";
        std::cout << "  Capture errors:  " << diag.captureErrors << "\n\n";
    }

    std::cout << "Master Audio Bus:\n";
    std::cout << "  Format:             " << diag.masterFormat.formatString() << "\n";
    std::cout << "  Capacity:           " << diag.busCapacityFrames << " frames\n";
    std::cout << "  Available:          " << diag.busAvailableFrames << " frames\n";
    std::cout << "  Produced:           " << diag.busFramesWritten << " frames\n";
    std::cout << "  Consumed by Router: " << diag.busFramesRead << " frames\n";
    std::cout << "  Bus underruns:      " << diag.busUnderruns << "\n";
    std::cout << "  Bus overruns:       " << diag.busOverruns << "\n";
    std::cout << "  Router distributed: " << diag.routerFramesDistributed << " frames\n\n";

    printOutputTelemetry(diag.outputs, diag.pairwiseDrift);

    return 0;
}

int CommandInterface::handleToneCommand(const std::vector<std::string>& args) {
    std::vector<std::string> deviceSelectors;
    double frequency = 440.0;
    double duration = 5.0;
    double volume = 0.25;
    std::string delayStr;
    std::string syncMode;
    std::string offsetsStr;
    std::string csvPath;

    for (size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "--device" || args[i] == "-d" || args[i] == "--output" || args[i] == "-o") && i + 1 < args.size()) {
            deviceSelectors.push_back(args[++i]);
        } else if ((args[i] == "--outputs" || args[i] == "-O") && i + 1 < args.size()) {
            auto items = splitString(args[++i], ',');
            deviceSelectors.insert(deviceSelectors.end(), items.begin(), items.end());
        } else if ((args[i] == "--frequency" || args[i] == "-f") && i + 1 < args.size()) {
            try { frequency = std::stod(args[++i]); } catch (...) {}
        } else if ((args[i] == "--duration" || args[i] == "-t") && i + 1 < args.size()) {
            try { duration = std::stod(args[++i]); } catch (...) {}
        } else if ((args[i] == "--volume" || args[i] == "-v") && i + 1 < args.size()) {
            try { volume = std::stod(args[++i]); } catch (...) {}
        } else if (args[i] == "--delay" && i + 1 < args.size()) {
            delayStr = args[++i];
        } else if (args[i] == "--sync" && i + 1 < args.size()) {
            syncMode = args[++i];
        } else if (args[i] == "--offsets" && i + 1 < args.size()) {
            offsetsStr = args[++i];
        } else if ((args[i] == "--log-csv" || args[i] == "--csv") && i + 1 < args.size()) {
            csvPath = args[++i];
        }
    }

    auto resolveDevice = [this](const std::string& selector) -> std::optional<AudioDevice> {
        if (selector.empty()) {
            return deviceManager_->getDefaultDevice();
        }
        bool isIndex = !selector.empty() && 
                       std::all_of(selector.begin(), selector.end(), ::isdigit);
        if (isIndex) {
            size_t idx = std::stoul(selector);
            auto dev = deviceManager_->getDeviceByIndex(idx, true);
            if (!dev) {
                dev = deviceManager_->getDeviceByIndex(idx, false);
            }
            return dev;
        }
        return deviceManager_->getDeviceById(selector);
    };

    std::vector<AudioDevice> targetDevs;
    if (deviceSelectors.empty()) {
        auto defDev = deviceManager_->getDefaultDevice();
        if (!defDev) {
            std::cerr << "Error: No default audio render device found on host.\n";
            return 1;
        }
        targetDevs.push_back(*defDev);
    } else {
        for (const auto& sel : deviceSelectors) {
            auto dev = resolveDevice(sel);
            if (!dev) {
                std::cerr << "Error: Audio device '" << sel << "' not found.\n";
                return 1;
            }
            targetDevs.push_back(*dev);
        }
    }

    if (!delayStr.empty()) {
        auto delays = parseDoubleList(delayStr);
        audioEngine_->setManualDelays(delays);
    }
    if (!offsetsStr.empty()) {
        auto offsets = parseDoubleList(offsetsStr);
        audioEngine_->setCalibrationOffsets(offsets);
    }
    if (!syncMode.empty()) {
        if (syncMode == "adaptive" || syncMode == "drift") {
            audioEngine_->alignSoftwareLatencies();
            audioEngine_->enableDriftCorrection(true);
            std::cout << "  Sync Mode: Automatic Software Alignment + Adaptive Micro-Resampling Drift Correction\n";
        } else if (syncMode == "software" || syncMode == "auto") {
            audioEngine_->alignSoftwareLatencies();
            audioEngine_->enableDriftCorrection(false);
            std::cout << "  Sync Mode: Static Software Latency Alignment\n";
        } else if (syncMode == "none" || syncMode == "off") {
            audioEngine_->setManualDelays({});
            audioEngine_->enableDriftCorrection(false);
        }
    }

    ToneParameters params;
    params.frequencyHz = frequency;
    params.volume = volume;
    params.durationSec = duration;

    std::cout << "\nSyncWave Tone Generator (Multi-Output Routing Pipeline)\n";
    std::cout << "=======================================================\n\n";

    std::cout << "Target Output Endpoints (" << targetDevs.size() << "):\n";
    for (size_t i = 0; i < targetDevs.size(); ++i) {
        std::cout << "  [" << i << "] " << targetDevs[i].name << " (ID: " << targetDevs[i].id << ")\n";
    }
    std::cout << "\n";

    deviceManager_->startMonitoring([&](const DeviceEvent& ev) {
        if (ev.type == DeviceEventType::Removed || 
           (ev.type == DeviceEventType::StateChanged && ev.newState != DeviceState::Active)) {
            audioEngine_->onDeviceDisconnected(ev.deviceId);
        }
    });

    bool started = audioEngine_->startTone(targetDevs, params);
    if (!started) {
        std::cerr << "Error: Failed to start playback through Master Audio Bus and Output Router.\n";
        deviceManager_->stopMonitoring();
        return 1;
    }

    auto diag = audioEngine_->getDiagnostics();

    std::cout << "Pipeline Architecture:\n";
    std::cout << "  ToneGenerator -> MasterAudioBus -> OutputRouter -> [Per-Output Queues] -> [Resamplers] -> [WasapiOutputs]\n\n";

    std::cout << "Master Audio Bus:\n";
    std::cout << "  Format:    " << diag.masterFormat.formatString() << "\n";
    std::cout << "  Capacity:  " << diag.busCapacityFrames << " frames (~" 
              << (diag.busCapacityFrames * 1000 / diag.masterFormat.sampleRate) << " ms)\n\n";

    std::cout << "Tone Parameters:\n";
    std::cout << "  Frequency: " << frequency << " Hz\n";
    std::cout << "  Volume:    " << volume << "\n";
    if (duration > 0) {
        std::cout << "  Duration:  " << duration << " second(s)\n";
    } else {
        std::cout << "  Duration:  Continuous\n";
    }

    std::cout << "\nPlayback started across all target endpoints.\n";
    std::cout << "Press Ctrl+C to stop.\n\n" << std::flush;

    g_stopRequested.store(false);
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    std::ofstream csvFile;
    if (!csvPath.empty()) {
        csvFile.open(csvPath);
        if (csvFile.is_open()) {
            csvFile << "timestamp_sec,device_index,device_name,target_playhead_sec,output_playhead_sec,"
                    << "raw_phase_error_ms,filtered_phase_error_ms,raw_drift_ppm,filtered_drift_ppm,"
                    << "feedforward_ppm,proportional_ppm,commanded_ppm,target_adjustment_ppm,"
                    << "current_adjustment_ppm,queue_available_frames,delay_buffer_frames,"
                    << "controller_state,wasapi_underruns,queue_underruns,queue_overruns\n";
            std::cout << "Logging 10 Hz telemetry to: " << csvPath << "\n\n";
        } else {
            std::cerr << "Warning: Could not open CSV log file: " << csvPath << "\n";
        }
    }

    auto startTime = std::chrono::steady_clock::now();
    auto lastSampleTime = startTime;
    auto lastPrintTime = startTime;

    while (!g_stopRequested.load()) {
        if (!audioEngine_->isRunning()) {
            std::cout << "\nAll audio endpoints became unavailable.\nPlayback stopped.\n" << std::flush;
            break;
        }

        if (duration > 0.0) {
            auto elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed >= duration) {
                break;
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (now - lastSampleTime >= std::chrono::milliseconds(100)) {
            audioEngine_->sampleClocks();
            lastSampleTime = now;

            auto outputs = audioEngine_->outputRouter().getOutputTelemetry();
            double elapsedSec = std::chrono::duration<double>(now - startTime).count();

            if (csvFile.is_open()) {
                for (size_t i = 0; i < outputs.size(); ++i) {
                    const auto& out = outputs[i];
                    csvFile << std::fixed << std::setprecision(4)
                            << elapsedSec << ","
                            << i << ",\""
                            << out.deviceName << "\","
                            << out.syncError.targetPlayheadSec << ","
                            << out.syncError.outputPlayheadSec << ","
                            << out.syncError.phaseErrorMs << ","
                            << out.filteredPhaseErrorMs << ","
                            << out.rawDriftPpm << ","
                            << out.filteredDriftPpm << ","
                            << out.feedforwardTermPpm << ","
                            << out.proportionalTermPpm << ","
                            << out.commandedPpm << ","
                            << out.targetRateAdjustmentPpm << ","
                            << out.currentRateAdjustmentPpm << ","
                            << out.queueAvailable << ","
                            << out.appliedDelayFrames << ",\""
                            << driftCorrectionStateToString(out.driftState) << "\","
                            << out.wasapiUnderruns << ","
                            << out.queueUnderruns << ","
                            << out.queueOverruns << "\n";
                }
                csvFile.flush();
            }

            // Print live update every 5 seconds
            if (now - lastPrintTime >= std::chrono::seconds(5)) {
                lastPrintTime = now;
                std::cout << "[" << std::setw(5) << std::fixed << std::setprecision(1) << elapsedSec << "s]";
                for (size_t i = 0; i < outputs.size(); ++i) {
                    const auto& out = outputs[i];
                    std::cout << " [" << i << "]: phase=" << std::showpos << std::fixed << std::setprecision(2)
                              << out.filteredPhaseErrorMs << "ms drift=" << std::setprecision(1)
                              << out.filteredDriftPpm << "ppm adj=" << out.currentRateAdjustmentPpm
                              << "ppm (" << driftCorrectionStateToString(out.driftState) << ")" << std::noshowpos;
                }
                std::cout << "\n" << std::flush;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (csvFile.is_open()) {
        csvFile.close();
    }

    audioEngine_->stop();
    deviceManager_->stopMonitoring();
    SetConsoleCtrlHandler(consoleCtrlHandler, FALSE);

    auto finalDiag = audioEngine_->getDiagnostics();

    std::cout << "\nPlayback complete.\n\n";
    std::cout << "Master Audio Bus Telemetry:\n";
    std::cout << "  Capacity:            " << finalDiag.busCapacityFrames << " frames\n";
    std::cout << "  Frames produced:     " << finalDiag.busFramesWritten << "\n";
    std::cout << "  Consumed by Router:  " << finalDiag.busFramesRead << "\n";
    std::cout << "  Router distributed:  " << finalDiag.routerFramesDistributed << "\n";
    std::cout << "  Bus underruns:       " << finalDiag.busUnderruns << "\n";
    std::cout << "  Bus overruns:        " << finalDiag.busOverruns << "\n\n";

    printOutputTelemetry(finalDiag.outputs, finalDiag.pairwiseDrift);
    std::cout << std::flush;

    return 0;
}

int CommandInterface::handleCaptureCommand(const std::vector<std::string>& args) {
    std::string sourceSelector;
    std::vector<std::string> outputSelectors;
    double duration = 5.0;
    std::string delayStr;
    std::string syncMode;
    std::string offsetsStr;

    for (size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "--source" || args[i] == "-s") && i + 1 < args.size()) {
            sourceSelector = args[++i];
        } else if ((args[i] == "--output" || args[i] == "-o") && i + 1 < args.size()) {
            outputSelectors.push_back(args[++i]);
        } else if ((args[i] == "--outputs" || args[i] == "-O") && i + 1 < args.size()) {
            auto items = splitString(args[++i], ',');
            outputSelectors.insert(outputSelectors.end(), items.begin(), items.end());
        } else if ((args[i] == "--duration" || args[i] == "-t") && i + 1 < args.size()) {
            try { duration = std::stod(args[++i]); } catch (...) {}
        } else if (args[i] == "--delay" && i + 1 < args.size()) {
            delayStr = args[++i];
        } else if (args[i] == "--sync" && i + 1 < args.size()) {
            syncMode = args[++i];
        } else if (args[i] == "--offsets" && i + 1 < args.size()) {
            offsetsStr = args[++i];
        }
    }

    auto resolveDevice = [this](const std::string& selector) -> std::optional<AudioDevice> {
        if (selector.empty()) {
            return deviceManager_->getDefaultDevice();
        }
        bool isIndex = !selector.empty() && 
                       std::all_of(selector.begin(), selector.end(), ::isdigit);
        if (isIndex) {
            size_t idx = std::stoul(selector);
            auto dev = deviceManager_->getDeviceByIndex(idx, true);
            if (!dev) {
                dev = deviceManager_->getDeviceByIndex(idx, false);
            }
            return dev;
        }
        return deviceManager_->getDeviceById(selector);
    };

    auto sourceDev = resolveDevice(sourceSelector);
    if (!sourceDev) {
        std::cerr << "Error: Capture source endpoint '" << sourceSelector << "' not found.\n";
        return 1;
    }

    std::vector<AudioDevice> outputDevs;
    if (outputSelectors.empty()) {
        auto defDev = deviceManager_->getDefaultDevice();
        if (!defDev) {
            std::cerr << "Error: No default audio render device found on host.\n";
            return 1;
        }
        outputDevs.push_back(*defDev);
    } else {
        for (const auto& sel : outputSelectors) {
            auto dev = resolveDevice(sel);
            if (!dev) {
                std::cerr << "Error: Output destination endpoint '" << sel << "' not found.\n";
                return 1;
            }
            outputDevs.push_back(*dev);
        }
    }

    if (!delayStr.empty()) {
        auto delays = parseDoubleList(delayStr);
        audioEngine_->setManualDelays(delays);
    }
    if (!offsetsStr.empty()) {
        auto offsets = parseDoubleList(offsetsStr);
        audioEngine_->setCalibrationOffsets(offsets);
    }
    if (!syncMode.empty()) {
        if (syncMode == "adaptive" || syncMode == "drift") {
            audioEngine_->alignSoftwareLatencies();
            audioEngine_->enableDriftCorrection(true);
            std::cout << "  Sync Mode: Automatic Software Alignment + Adaptive Micro-Resampling Drift Correction\n";
        } else if (syncMode == "software" || syncMode == "auto") {
            audioEngine_->alignSoftwareLatencies();
            audioEngine_->enableDriftCorrection(false);
            std::cout << "  Sync Mode: Static Software Latency Alignment\n";
        } else if (syncMode == "none" || syncMode == "off") {
            audioEngine_->setManualDelays({});
            audioEngine_->enableDriftCorrection(false);
        }
    }

    std::cout << "\nSyncWave System Audio Capture (Multi-Output Routing Pipeline)\n";
    std::cout << "=============================================================\n\n";

    std::cout << "Capture Source:\n";
    std::cout << "  Device: " << sourceDev->name << "\n";
    std::cout << "  State:  " << sourceDev->stateString() << "\n";
    std::cout << "  ID:     " << sourceDev->id << "\n\n";

    std::cout << "Output Destinations (" << outputDevs.size() << "):\n";
    for (size_t i = 0; i < outputDevs.size(); ++i) {
        std::cout << "  [" << i << "] " << outputDevs[i].name << " (" << outputDevs[i].stateString() << ")\n";
        if (outputDevs[i].id == sourceDev->id) {
            std::cout << "      [NOTE] Output is identical to capture source. Using separate physical endpoints\n"
                      << "      is recommended to prevent acoustic feedback.\n";
        }
    }
    std::cout << "\n";

    deviceManager_->startMonitoring([&](const DeviceEvent& ev) {
        if (ev.type == DeviceEventType::Removed || 
           (ev.type == DeviceEventType::StateChanged && ev.newState != DeviceState::Active)) {
            audioEngine_->onDeviceDisconnected(ev.deviceId);
        }
    });

    bool started = audioEngine_->startCapture(*sourceDev, outputDevs);
    if (!started) {
        std::cerr << "Error: Failed to initialize and start multi-output loopback capture pipeline.\n";
        deviceManager_->stopMonitoring();
        return 1;
    }

    auto diag = audioEngine_->getDiagnostics();

    std::cout << "Pipeline Architecture:\n";
    std::cout << "  Windows Audio -> WASAPI Loopback Capture -> MasterAudioBus (RingBuffer) -> OutputRouter\n";
    std::cout << "               -> [Per-Output Queues] -> [Per-Output Resamplers] -> [WasapiOutputs] -> Hardware\n\n";

    std::cout << "Capture Stream:\n";
    std::cout << "  Format:      " << diag.captureFormat.formatString() << "\n\n";

    std::cout << "Master Audio Bus:\n";
    std::cout << "  Format:      " << diag.masterFormat.formatString() << "\n";
    std::cout << "  Capacity:    " << diag.busCapacityFrames << " frames (~" 
              << (diag.busCapacityFrames * 1000 / diag.masterFormat.sampleRate) << " ms)\n\n";

    if (duration > 0) {
        std::cout << "Duration:      " << duration << " second(s)\n";
    } else {
        std::cout << "Duration:      Continuous (Press Ctrl+C to stop)\n";
    }

    std::cout << "\nLoopback capture streaming active across all outputs.\n";
    std::cout << "Play any audio in Windows (browser, media player, games, etc.)\n";
    std::cout << "Press Ctrl+C to stop.\n\n" << std::flush;

    g_stopRequested.store(false);
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    auto startTime = std::chrono::steady_clock::now();
    auto lastSampleTime = startTime;

    while (!g_stopRequested.load()) {
        if (!audioEngine_->isRunning()) {
            std::cout << "\nAll audio endpoints became unavailable.\nCapture stopped.\n" << std::flush;
            break;
        }

        if (duration > 0.0) {
            auto elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed >= duration) {
                break;
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (now - lastSampleTime >= std::chrono::milliseconds(100)) {
            audioEngine_->sampleClocks();
            lastSampleTime = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    audioEngine_->stop();
    deviceManager_->stopMonitoring();
    SetConsoleCtrlHandler(consoleCtrlHandler, FALSE);

    auto finalDiag = audioEngine_->getDiagnostics();

    std::cout << "\nCapture complete.\n\n";
    std::cout << "WASAPI Loopback Capture Telemetry:\n";
    std::cout << "  Frames captured:     " << finalDiag.framesCaptured << "\n";
    std::cout << "  Packets captured:    " << finalDiag.packetsCaptured << "\n";
    std::cout << "  Silence packets:     " << finalDiag.silencePackets << "\n";
    std::cout << "  Discontinuities:     " << finalDiag.discontinuities << "\n";
    std::cout << "  Capture errors:      " << finalDiag.captureErrors << "\n\n";

    std::cout << "Master Audio Bus Telemetry:\n";
    std::cout << "  Capacity:            " << finalDiag.busCapacityFrames << " frames\n";
    std::cout << "  Frames written:      " << finalDiag.busFramesWritten << "\n";
    std::cout << "  Consumed by Router:  " << finalDiag.busFramesRead << "\n";
    std::cout << "  Router distributed:  " << finalDiag.routerFramesDistributed << " frames\n";
    std::cout << "  Bus underruns:       " << finalDiag.busUnderruns << "\n";
    std::cout << "  Bus overruns:        " << finalDiag.busOverruns << "\n\n";

    printOutputTelemetry(finalDiag.outputs, finalDiag.pairwiseDrift);
    std::cout << std::flush;

    return 0;
}

int CommandInterface::handleClockTestCommand(const std::vector<std::string>& args) {
    std::vector<std::string> outputSpecs;
    double durationSec = 60.0;

    for (size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "--outputs" || args[i] == "-O") && i + 1 < args.size()) {
            auto tokens = splitString(args[++i], ',');
            outputSpecs.insert(outputSpecs.end(), tokens.begin(), tokens.end());
        } else if ((args[i] == "--output" || args[i] == "-o" || args[i] == "--device" || args[i] == "-d") && i + 1 < args.size()) {
            outputSpecs.push_back(args[++i]);
        } else if ((args[i] == "--duration" || args[i] == "-t") && i + 1 < args.size()) {
            durationSec = std::stod(args[++i]);
        }
    }

    auto activeDevices = deviceManager_->enumerateDevices(true);
    if (activeDevices.empty()) {
        std::cerr << "Error: No active audio output devices available.\n";
        return 1;
    }

    std::vector<AudioDevice> targets;
    if (outputSpecs.empty()) {
        if (activeDevices.size() >= 2) {
            targets.push_back(activeDevices[0]);
            targets.push_back(activeDevices[1]);
        } else {
            targets.push_back(activeDevices[0]);
        }
    } else {
        for (const auto& spec : outputSpecs) {
            try {
                size_t idx = std::stoul(spec);
                if (idx < activeDevices.size()) {
                    targets.push_back(activeDevices[idx]);
                    continue;
                }
            } catch (...) {}
            auto dev = deviceManager_->getDeviceById(spec);
            if (dev) {
                targets.push_back(*dev);
            } else {
                std::cerr << "Error: Unknown output device: " << spec << "\n";
                return 1;
            }
        }
    }

    std::cout << "\nSyncWave High-Precision Clock & Drift Experiment\n";
    std::cout << "=================================================\n\n";
    std::cout << "Target Endpoints (" << targets.size() << "):\n";
    for (size_t i = 0; i < targets.size(); ++i) {
        std::cout << "  [" << i << "] " << targets[i].name << "\n";
    }
    std::cout << "\nTest Configuration:\n";
    std::cout << "  Duration:          " << durationSec << " seconds\n";
    std::cout << "  Sampling Rate:     10 Hz (every 100 ms out-of-band)\n";
    std::cout << "  Signal:            440 Hz continuous tone (volume 0.25)\n\n";

    g_stopRequested.store(false);
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    ToneParameters params{ 440.0, 0.25, 0.0 };
    if (!audioEngine_->startTone(targets, params)) {
        std::cerr << "Error: Failed to initialize multi-output playback for clock experiment.\n";
        SetConsoleCtrlHandler(consoleCtrlHandler, FALSE);
        return 1;
    }

    std::cout << "Clock experiment running. Press Ctrl+C to abort early.\n";

    auto startTime = std::chrono::steady_clock::now();
    while (!g_stopRequested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        audioEngine_->sampleClocks();

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - startTime).count();
        if (durationSec > 0.0 && elapsed >= durationSec) {
            break;
        }

        std::cout << "\r[Running] Elapsed: " << std::fixed << std::setprecision(1) << elapsed 
                  << " s / " << durationSec << " s" << std::flush;
    }
    std::cout << "\n\nStopping engine and compiling clock telemetry...\n";

    audioEngine_->stop();
    SetConsoleCtrlHandler(consoleCtrlHandler, FALSE);

    auto finalDiag = audioEngine_->getDiagnostics();

    // 1. Detailed per-endpoint clock summary
    std::cout << "\n=======================================================\n";
    std::cout << "               CLOCK TELEMETRY SUMMARY                 \n";
    std::cout << "=======================================================\n";

    for (size_t i = 0; i < finalDiag.outputs.size(); ++i) {
        const auto& out = finalDiag.outputs[i];
        std::cout << "\n[" << i << "] " << out.deviceName << "\n";
        std::cout << "    Nominal Rate:         " << out.format.sampleRate << " Hz\n";
        std::cout << "    Clock Frequency:      " << out.clockFrequency << " Hz (" 
                  << (out.format.sampleRate > 0 ? (out.clockFrequency / out.format.sampleRate) : 0) << " ticks/frame)\n";
        std::cout << "    Final Clock Position: " << out.clockPosition << " ticks ("
                  << (out.clockFrequency > 0 ? (static_cast<double>(out.clockPosition) / out.clockFrequency) : 0.0) << " s)\n";
        std::cout << "    Estimated Rate:       " << std::fixed << std::setprecision(2) << out.estimatedClockRateHz << " Hz\n";
        std::cout << "    Rate Error (PPM):     " << (out.rateErrorPpm >= 0.0 ? "+" : "") << out.rateErrorPpm << " ppm\n";
        std::cout << "    Samples Recorded:     " << out.clockSampleCount << " samples\n";
        std::cout << "    Measurement Span:     " << out.measurementDurationSec << " s\n";
        std::cout << "    Current Padding:      " << out.currentPadding << " frames ("
                  << (out.format.sampleRate > 0 ? (out.currentPadding * 1000.0 / out.format.sampleRate) : 0.0) << " ms)\n";
        std::cout << "    WASAPI Underruns:     " << out.wasapiUnderruns << "\n";
    }

    // 2. Multi-Window Convergence Analysis Table
    std::cout << "\n=======================================================\n";
    std::cout << "         MULTI-WINDOW CLOCK STABILITY ANALYSIS         \n";
    std::cout << "=======================================================\n";
    std::cout << "Evaluating rate convergence across trailing time windows:\n\n";

    std::vector<double> windows = { 1.0, 5.0, 10.0, 30.0, 60.0 };

    for (size_t i = 0; i < finalDiag.outputs.size(); ++i) {
        auto* devOut = audioEngine_->outputRouter().getOutput(i);
        if (!devOut) continue;
        std::cout << "Device [" << i << "]: " << devOut->deviceName() << "\n";
        std::cout << "  Window      Estimated Rate       Rate Error (PPM)     Goodness-of-Fit (r^2)\n";
        std::cout << "  ---------------------------------------------------------------------------\n";

        for (double win : windows) {
            auto est = devOut->clock().estimateRateOverWindow(win);
            if (est.isValid) {
                std::cout << "  " << std::setw(4) << static_cast<int>(win) << " s       "
                          << std::setw(12) << std::fixed << std::setprecision(2) << est.estimatedRate << " Hz       "
                          << std::setw(11) << (est.rateErrorPpm >= 0.0 ? "+" : "") << est.rateErrorPpm << " ppm        "
                          << std::setw(6) << std::setprecision(5) << est.rSquared << "\n";
            } else {
                std::cout << "  " << std::setw(4) << static_cast<int>(win) << " s       (insufficient samples in window)\n";
            }
        }
        std::cout << "\n";
    }

    // 3. Pairwise Relative Drift & Offset Separation
    std::cout << "=======================================================\n";
    std::cout << "         PAIRWISE DRIFT & OFFSET SEPARATION            \n";
    std::cout << "=======================================================\n";

    auto pairs = audioEngine_->outputRouter().getPairwiseDriftEstimates();
    if (pairs.empty()) {
        std::cout << "  (Single endpoint configured - no pairwise combinations)\n\n";
    } else {
        for (const auto& pair : pairs) {
            std::cout << "  " << pair.deviceNameA << " <-> " << pair.deviceNameB << ":\n";
            if (pair.isValid) {
                std::cout << "      Relative Drift Rate:  " << (pair.driftRatePpm >= 0.0 ? "+" : "") << pair.driftRatePpm << " ppm\n";
                std::cout << "      Normalized Ratio:     " << std::setprecision(6) << pair.rateRatio << " (A / B)\n";
                std::cout << "      Initial Offset:       " << std::setprecision(2) << (pair.initialOffsetSec * 1000.0) << " ms\n";
                std::cout << "      Final Offset:         " << (pair.instantaneousOffsetSec * 1000.0) << " ms\n";
                std::cout << "      Accumulated Drift:    " << (pair.accumulatedDriftSec * 1000.0) << " ms\n";
                std::cout << "      Measurement Duration: " << pair.measurementDurationSec << " s\n";
                std::cout << "      Confidence (r^2):     " << std::setprecision(4) << pair.confidence << "\n";
            } else {
                std::cout << "      Status: accumulating timing samples...\n";
            }
            std::cout << "\n";
        }
    }

    std::cout << "[TIMING NOTE] Software timestamps and WASAPI stream latency do NOT measure physical acoustic latency.\n"
              << "Hardware DAC filtering, Bluetooth A2DP transport buffering, and speaker drivers introduce additional delay.\n"
              << "Delay alignment and acoustic synchronization will be implemented in Milestones 8+.\n\n";

    return 0;
}

int CommandInterface::handleLatencyTestCommand(const std::vector<std::string>& args) {
    std::vector<std::string> outputSpecs;
    int numRuns = 5;
    std::string delayStr;
    std::string syncMode;
    std::string offsetsStr;

    for (size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "--outputs" || args[i] == "-O") && i + 1 < args.size()) {
            auto tokens = splitString(args[++i], ',');
            outputSpecs.insert(outputSpecs.end(), tokens.begin(), tokens.end());
        } else if ((args[i] == "--output" || args[i] == "-o" || args[i] == "--device" || args[i] == "-d") && i + 1 < args.size()) {
            outputSpecs.push_back(args[++i]);
        } else if ((args[i] == "--runs" || args[i] == "-n") && i + 1 < args.size()) {
            numRuns = std::stoi(args[++i]);
            if (numRuns < 1) numRuns = 1;
        } else if (args[i] == "--delay" && i + 1 < args.size()) {
            delayStr = args[++i];
        } else if (args[i] == "--sync" && i + 1 < args.size()) {
            syncMode = args[++i];
        } else if (args[i] == "--offsets" && i + 1 < args.size()) {
            offsetsStr = args[++i];
        }
    }

    auto activeDevices = deviceManager_->enumerateDevices(true);
    if (activeDevices.size() < 2) {
        std::cerr << "Error: Relative latency test requires at least 2 active audio output endpoints (found " 
                  << activeDevices.size() << ").\n";
        return 1;
    }

    std::vector<AudioDevice> targets;
    if (outputSpecs.empty()) {
        targets.push_back(activeDevices[0]);
        targets.push_back(activeDevices[1]);
    } else {
        for (const auto& spec : outputSpecs) {
            try {
                size_t idx = std::stoul(spec);
                if (idx < activeDevices.size()) {
                    targets.push_back(activeDevices[idx]);
                    continue;
                }
            } catch (...) {}
            auto dev = deviceManager_->getDeviceById(spec);
            if (dev) {
                targets.push_back(*dev);
            } else {
                std::cerr << "Error: Unknown output device: " << spec << "\n";
                return 1;
            }
        }
    }

    if (targets.size() < 2) {
        std::cerr << "Error: Please select at least 2 output endpoints for relative latency comparison.\n";
        return 1;
    }

    std::cout << "\nSyncWave Deterministic Transient & Software Latency Test\n";
    std::cout << "========================================================\n\n";
    std::cout << "Target Endpoints (" << targets.size() << "):\n";
    for (size_t i = 0; i < targets.size(); ++i) {
        std::cout << "  [" << i << "] " << targets[i].name << "\n";
    }
    std::cout << "\nSignal Specification:\n";
    std::cout << "  Type:              SyncPulseGenerator\n";
    std::cout << "  Structure:         500 ms silence -> 1 ms impulse (peak 1.0f) -> 500 ms silence\n";
    std::cout << "  Repeated Runs:     " << numRuns << "\n";

    if (!delayStr.empty()) {
        auto delays = parseDoubleList(delayStr);
        audioEngine_->setManualDelays(delays);
        std::cout << "  Manual Delays:     " << delayStr << " ms\n";
    }
    if (!offsetsStr.empty()) {
        auto offsets = parseDoubleList(offsetsStr);
        audioEngine_->setCalibrationOffsets(offsets);
        std::cout << "  Calibration Offs:  " << offsetsStr << " ms\n";
    }
    if (!syncMode.empty()) {
        if (syncMode == "adaptive" || syncMode == "drift") {
            audioEngine_->alignSoftwareLatencies();
            audioEngine_->enableDriftCorrection(true);
            std::cout << "  Sync Mode:         Automatic Software Alignment + Adaptive Micro-Resampling Drift Correction\n";
        } else if (syncMode == "software" || syncMode == "auto") {
            audioEngine_->alignSoftwareLatencies();
            audioEngine_->enableDriftCorrection(false);
            std::cout << "  Sync Mode:         Automatic Software Latency Alignment\n";
        } else if (syncMode == "none" || syncMode == "off") {
            audioEngine_->setManualDelays({});
            audioEngine_->enableDriftCorrection(false);
        }
    }
    std::cout << "\n";

    g_stopRequested.store(false);
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    std::vector<double> relativeLatenciesMs;
    relativeLatenciesMs.reserve(numRuns);

    PulseParameters pulseParams;
    pulseParams.leadInFrames = 24000;      // 500 ms @ 48kHz
    pulseParams.pulseDurationFrames = 48;  // 1 ms @ 48kHz
    pulseParams.leadOutFrames = 24000;     // 500 ms @ 48kHz
    pulseParams.peakAmplitude = 1.0f;

    for (int run = 1; run <= numRuns && !g_stopRequested.load(); ++run) {
        std::cout << "Run " << run << " of " << numRuns << "... " << std::flush;

        if (!audioEngine_->startPulse(targets, pulseParams)) {
            std::cerr << "\nError: Failed to launch pulse generator on run " << run << "\n";
            break;
        }

        // Wait for pulse sequence to play (~1.2 seconds)
        auto pulseStart = std::chrono::steady_clock::now();
        while (!g_stopRequested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            audioEngine_->sampleClocks();

            auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - pulseStart).count();
            if (elapsed >= 1.2) {
                break;
            }
        }

        auto diag = audioEngine_->getDiagnostics();
        audioEngine_->stop();

        if (diag.outputs.size() >= 2) {
            const auto& out0 = diag.outputs[0];
            const auto& out1 = diag.outputs[1];

            // Software relative latency: playhead difference at measurement end
            double deltaMs = (out0.estimatedAppPlayheadSec - out1.estimatedAppPlayheadSec) * 1000.0;
            relativeLatenciesMs.push_back(deltaMs);

            std::cout << "Relative Software Latency: " << std::fixed << std::setprecision(2) << deltaMs << " ms "
                      << "(Playhead0: " << (out0.estimatedAppPlayheadSec * 1000.0) << " ms, "
                      << "Playhead1: " << (out1.estimatedAppPlayheadSec * 1000.0) << " ms)\n";
        } else {
            std::cout << "(insufficient outputs active in run)\n";
        }

        // Brief cooldown between runs
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    SetConsoleCtrlHandler(consoleCtrlHandler, FALSE);

    if (relativeLatenciesMs.empty()) {
        std::cerr << "No successful test runs recorded.\n";
        return 1;
    }

    // Compute repeatability statistics
    double sum = 0.0;
    double minVal = relativeLatenciesMs[0];
    double maxVal = relativeLatenciesMs[0];
    for (double val : relativeLatenciesMs) {
        sum += val;
        minVal = std::min(minVal, val);
        maxVal = std::max(maxVal, val);
    }
    double mean = sum / relativeLatenciesMs.size();

    auto sortedVals = relativeLatenciesMs;
    std::sort(sortedVals.begin(), sortedVals.end());
    double median = (sortedVals.size() % 2 == 1)
                        ? sortedVals[sortedVals.size() / 2]
                        : (sortedVals[sortedVals.size() / 2 - 1] + sortedVals[sortedVals.size() / 2]) / 2.0;

    double varSum = 0.0;
    for (double val : relativeLatenciesMs) {
        double diff = val - mean;
        varSum += diff * diff;
    }
    double stdDev = (relativeLatenciesMs.size() > 1) ? std::sqrt(varSum / (relativeLatenciesMs.size() - 1)) : 0.0;

    std::cout << "\n=======================================================\n";
    std::cout << "       TRANSIENT TEST REPEATABILITY STATISTICS         \n";
    std::cout << "=======================================================\n";
    std::cout << "Outputs Compared: " << targets[0].name << " vs " << targets[1].name << "\n";
    std::cout << "Successful Runs:  " << relativeLatenciesMs.size() << " / " << numRuns << "\n";
    std::cout << "Mean Latency:     " << std::fixed << std::setprecision(2) << mean << " ms\n";
    std::cout << "Median Latency:   " << median << " ms\n";
    std::cout << "Min Latency:      " << minVal << " ms\n";
    std::cout << "Max Latency:      " << maxVal << " ms\n";
    std::cout << "Std Deviation:    " << stdDev << " ms\n\n";

    std::cout << "=======================================================\n";
    std::cout << "       PHYSICAL / ACOUSTIC LATENCY ASSESSMENT          \n";
    std::cout << "=======================================================\n";
    std::cout << "Status:           PHYSICAL ACOUSTIC LATENCY NOT DIRECTLY MEASURABLE WITH CURRENT SETUP\n";
    std::cout << "Explanation:      Windows software timestamps (IAudioClock and QPC) and WASAPI stream\n"
              << "                  latency observe only the OS mixing and driver submission stages.\n"
              << "                  Physical acoustic emission includes external physical delays:\n"
              << "                    - Bluetooth A2DP transport packetization and RF transmission delay\n"
              << "                    - Hardware DAC reconstruction / anti-aliasing filter delay\n"
              << "                    - Transducer electromechanical latency and room air flight time\n"
              << "                  Direct measurement of physical acoustic latency requires an external\n"
              << "                  calibrated microphone feedback loop or oscilloscope probe.\n"
              << "                  Physical delay alignment is scheduled for Milestones 9+.\n\n";

    return 0;
}

int CommandInterface::handleCalibrateCommand(const std::vector<std::string>& args) {
    std::vector<std::string> outputSpecs;
    std::string offsetsStr;

    for (size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "--outputs" || args[i] == "-O") && i + 1 < args.size()) {
            auto tokens = splitString(args[++i], ',');
            outputSpecs.insert(outputSpecs.end(), tokens.begin(), tokens.end());
        } else if ((args[i] == "--output" || args[i] == "-o" || args[i] == "--device" || args[i] == "-d") && i + 1 < args.size()) {
            outputSpecs.push_back(args[++i]);
        } else if (args[i] == "--offsets" && i + 1 < args.size()) {
            offsetsStr = args[++i];
        }
    }

    auto activeDevices = deviceManager_->enumerateDevices(true);
    if (activeDevices.empty()) {
        std::cerr << "Error: No active audio output endpoints found for calibration.\n";
        return 1;
    }

    std::vector<AudioDevice> targets;
    if (outputSpecs.empty()) {
        if (activeDevices.size() >= 2) {
            targets.push_back(activeDevices[0]);
            targets.push_back(activeDevices[1]);
        } else {
            targets.push_back(activeDevices[0]);
        }
    } else {
        for (const auto& spec : outputSpecs) {
            try {
                size_t idx = std::stoul(spec);
                if (idx < activeDevices.size()) {
                    targets.push_back(activeDevices[idx]);
                    continue;
                }
            } catch (...) {}
            auto dev = deviceManager_->getDeviceById(spec);
            if (dev) {
                targets.push_back(*dev);
            } else {
                std::cerr << "Error: Unknown output device: " << spec << "\n";
                return 1;
            }
        }
    }

    std::cout << "\nSyncWave Software Latency Calibration & Delay Assessment\n";
    std::cout << "=========================================================\n\n";
    std::cout << "Probing " << targets.size() << " target endpoint(s) for WASAPI stream & buffer latencies...\n";

    ToneParameters params;
    params.frequencyHz = 440.0;
    params.volume = 0.0001; // nearly silent probe
    params.durationSec = 0.5;

    if (!audioEngine_->startTone(targets, params)) {
        std::cerr << "Error: Failed to probe audio endpoints via AudioEngine.\n";
        return 1;
    }

    // Allow WASAPI streams to initialize and buffer to fill
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    audioEngine_->sampleClocks();

    auto models = audioEngine_->outputRouter().getLatencyModels();

    CalibrationStore store;
    if (!offsetsStr.empty()) {
        auto offsets = parseDoubleList(offsetsStr);
        for (size_t i = 0; i < models.size() && i < offsets.size(); ++i) {
            models[i].optionalCalibrationOffsetMs = offsets[i];
            models[i].recalculate();
        }
    } else {
        for (size_t i = 0; i < models.size(); ++i) {
            auto rec = store.get(models[i].deviceId);
            if (rec && rec->confidence >= 0.35f) {
                models[i].optionalCalibrationOffsetMs = rec->measuredLatencyMs;
                models[i].recalculate();
            }
        }
    }

    auto plan = SyncController::computeSoftwareAlignmentPlan(models);

    audioEngine_->stop();

    std::cout << "\nDevice Latency Breakdown (Model B: End-to-End Acoustic Arrival Alignment):\n";
    std::cout << "---------------------------------------------------------------------------------------------------------------------------------------------\n";
    std::cout << "  Idx Device Name               Rate (Hz)  WASAPI Stream  Padding (ms)  Est. SW (ms)  Cal. Offset (ms)  Effective (ms)  Delay (ms)  Delay (frames)\n";
    std::cout << "---------------------------------------------------------------------------------------------------------------------------------------------\n";

    for (size_t i = 0; i < models.size(); ++i) {
        const auto& m = models[i];
        double delayMs = (i < plan.calculatedDelaysMs.size()) ? plan.calculatedDelaysMs[i] : 0.0;
        size_t delayFrames = (i < plan.calculatedDelaysFrames.size()) ? plan.calculatedDelaysFrames[i] : 0;
        std::string dName = m.deviceName;
        if (dName.length() > 24) dName = dName.substr(0, 21) + "...";

        std::cout << "  [" << i << "] "
                  << std::left << std::setw(25) << dName
                  << std::right << std::setw(10) << m.sampleRate << "  "
                  << std::setw(12) << std::fixed << std::setprecision(2) << m.wasapiStreamLatencyMs << "  "
                  << std::setw(12) << m.wasapiPaddingMs << "  "
                  << std::setw(12) << m.estimatedSoftwareLatencyMs << "  "
                  << std::setw(16) << m.optionalCalibrationOffsetMs << "  "
                  << std::setw(14) << m.effectiveLatencyMs << "  "
                  << std::setw(10) << delayMs << "  "
                  << std::setw(14) << delayFrames << "\n";
    }
    std::cout << "---------------------------------------------------------------------------------------------------------------------------------------------\n";
    std::cout << "Target Alignment Latency: " << std::fixed << std::setprecision(2) << plan.targetLatencyMs << " ms\n";
    std::cout << "Sync State:               " << syncStateToString(plan.syncState) << "\n\n";

    std::cout << "=======================================================================\n";
    std::cout << "         MODEL B: END-TO-END ACOUSTIC ARRIVAL ACCOUNTING               \n";
    std::cout << "=======================================================================\n";
    std::cout << "1. CALIBRATED DEVICES (SyncState::PhysicallyCalibrated):\n";
    std::cout << "   Effective latency equals measured end-to-end arrival offset (L_arrival).\n";
    std::cout << "   Software latency is NOT added again to prevent double-counting.\n";
    std::cout << "2. UNCALIBRATED DEVICES (SyncState::SoftwareCalibrated):\n";
    std::cout << "   Effective latency defaults to observable software latency (Est. SW).\n";
    std::cout << "3. DELAY ALIGNMENT:\n";
    std::cout << "   Each endpoint's delay buffer applies Delay = L_target - L_effective,\n";
    std::cout << "   ensuring simultaneous physical acoustic emission at the listener point.\n\n";

    return 0;
}

int CommandInterface::handleAcousticCalibrateCommand(const std::vector<std::string>& args) {
    std::string deviceSelector;
    std::string micSelector;
    int runs = 7;
    int minValid = 5;
    double chirpDuration = 0.150;
    double volume = 0.25;
    double appliedDelayMs = 0.0;
    bool saveToStore = true;

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--help" || args[i] == "-h") {
            std::cout << "\nUsage: syncwave acoustic-calibrate [options]\n\n"
                      << "Measures end-to-end acoustic arrival offset via chirp emission and microphone capture.\n"
                      << "NOTE: Measured arrival latency includes output software path + DAC/driver buffering +\n"
                      << "transducer response + acoustic propagation + microphone capture chain.\n\n"
                      << "Options:\n"
                      << "  --device, -d <id/index>       Target output device to calibrate (default: default render device)\n"
                      << "  --microphone, -m <id/index>   Reference microphone capture device (default: default capture device)\n"
                      << "  --runs, -r <n>                Number of calibration sweeps (default: 7)\n"
                      << "  --min-valid <n>               Minimum valid sweeps required (default: 5)\n"
                      << "  --delay <ms>                  Intentional delay applied via DelayBuffer (default: 0.0)\n"
                      << "  --chirp-duration <sec>        Chirp signal duration in seconds (default: 0.150)\n"
                      << "  --volume, -v <amplitude>      Chirp amplitude 0.05 - 0.50 (default: 0.25)\n"
                      << "  --save                        Persist result to calibration store (default)\n"
                      << "  --no-save                     Do not persist calibration result\n\n";
            return 0;
        } else if ((args[i] == "--device" || args[i] == "-d" || args[i] == "-o") && i + 1 < args.size()) {
            deviceSelector = args[++i];
        } else if ((args[i] == "--microphone" || args[i] == "-m" || args[i] == "-c") && i + 1 < args.size()) {
            micSelector = args[++i];
        } else if ((args[i] == "--runs" || args[i] == "-r" || args[i] == "-n") && i + 1 < args.size()) {
            try { runs = std::stoi(args[++i]); } catch (...) {}
        } else if (args[i] == "--min-valid" && i + 1 < args.size()) {
            try { minValid = std::stoi(args[++i]); } catch (...) {}
        } else if (args[i] == "--delay" && i + 1 < args.size()) {
            try { appliedDelayMs = std::stod(args[++i]); } catch (...) {}
        } else if (args[i] == "--chirp-duration" && i + 1 < args.size()) {
            try { chirpDuration = std::stod(args[++i]); } catch (...) {}
        } else if ((args[i] == "--volume" || args[i] == "-v") && i + 1 < args.size()) {
            try { volume = std::stod(args[++i]); } catch (...) {}
        } else if (args[i] == "--no-save") {
            saveToStore = false;
        } else if (args[i] == "--save") {
            saveToStore = true;
        }
    }

    // Safety clamp on volume: calibration must never exceed 0.50
    if (volume > 0.50) {
        std::cout << "[SAFETY] Volume clamped from " << volume << " to 0.50 maximum safe limit.\n";
        volume = 0.50;
    }
    if (volume < 0.05) {
        volume = 0.05;
    }

    // Resolve output device
    std::optional<AudioDevice> outputDev;
    if (deviceSelector.empty()) {
        outputDev = deviceManager_->getDefaultDevice();
    } else {
        bool isIndex = std::all_of(deviceSelector.begin(), deviceSelector.end(), ::isdigit);
        if (isIndex) {
            outputDev = deviceManager_->getDeviceByIndex(std::stoul(deviceSelector), true);
            if (!outputDev) outputDev = deviceManager_->getDeviceByIndex(std::stoul(deviceSelector), false);
        } else {
            outputDev = deviceManager_->getDeviceById(deviceSelector);
        }
    }

    if (!outputDev) {
        std::cerr << "Error: Target output audio device not found.\n";
        return 1;
    }

    // Resolve microphone device
    std::optional<AudioDevice> micDev;
    if (micSelector.empty()) {
        micDev = deviceManager_->getDefaultCaptureDevice();
    } else {
        bool isIndex = std::all_of(micSelector.begin(), micSelector.end(), ::isdigit);
        if (isIndex) {
            micDev = deviceManager_->getCaptureDeviceByIndex(std::stoul(micSelector), true);
            if (!micDev) micDev = deviceManager_->getCaptureDeviceByIndex(std::stoul(micSelector), false);
        } else {
            micDev = deviceManager_->getCaptureDeviceById(micSelector);
        }
    }

    if (!micDev) {
        std::cerr << "Error: Target microphone capture device not found.\n";
        return 1;
    }

    std::cout << "\nSyncWave Acoustic Arrival Latency Calibration (Milestone 11.1)\n";
    std::cout << "===============================================================\n\n";
    std::cout << "[METHODOLOGY NOTE] Measured value represents end-to-end acoustic arrival offset:\n";
    std::cout << "                   L_arrival = L_output_sw + L_DAC + L_driver + L_air + L_mic_capture\n";
    std::cout << "                   Relative arrival difference between outputs cancels shared mic latency.\n\n";
    std::cout << "[SAFETY WARNING] This calibration will emit an audible 300 Hz - 8 kHz swept chirp.\n";
    std::cout << "                 Please ensure speaker/headphone volume is at a comfortable level.\n\n";

    std::cout << "Configuration:\n";
    std::cout << "  Output Device:     " << outputDev->name << " (ID: " << outputDev->id << ")\n";
    std::cout << "  Microphone:        " << micDev->name << " (ID: " << micDev->id << ")\n";
    std::cout << "  Calibration Signal:300 Hz -> 8000 Hz linear chirp (" << (chirpDuration * 1000.0) << " ms, vol: " << volume << ")\n";
    std::cout << "  Requested Runs:    " << runs << " (min valid required: " << minValid << ")\n";
    if (appliedDelayMs > 0.0) {
        std::cout << "  Applied Delay:     " << std::fixed << std::setprecision(2) << appliedDelayMs << " ms (DelayBuffer intentional compensation)\n";
    }
    std::cout << "\n";

    AcousticCalibrationConfig config;
    config.runs = static_cast<uint32_t>(std::max(1, runs));
    config.minValidRuns = static_cast<uint32_t>(std::max(1, minValid));
    config.appliedDelayMs = appliedDelayMs;
    config.chirpParams.durationSec = chirpDuration;
    config.chirpParams.amplitude = static_cast<float>(volume);

    AcousticCalibrator calibrator(config);

    auto progressCallback = [](uint32_t cur, uint32_t total, const SingleRunMeasurement& m) {
        std::cout << "  [Run " << cur << "/" << total << "] ";
        if (m.isSuccess) {
            std::cout << "Detected arrival: " << std::fixed << std::setprecision(2) << m.measuredLatencyMs << " ms"
                      << " | score: " << std::setprecision(2) << m.peakScore
                      << " | PNR: " << std::setprecision(1) << m.peakToNoiseRatio
                      << " | conf: " << std::setprecision(1) << (m.confidence * 100.0f) << "%";
            if (m.secondaryPeakScore > 0.05f) {
                std::cout << " | secPeak: " << std::setprecision(2) << m.secondaryPeakScore
                          << " (sep: " << std::showpos << std::setprecision(1) << m.peakSeparationMs << std::noshowpos << " ms, ratio: "
                          << std::setprecision(1) << m.peakToSecondaryRatio << "x)";
            }
            std::cout << "\n";
        } else {
            std::cout << "Rejected: " << (m.failureReason.empty() ? "Correlation failed" : m.failureReason)
                      << " (score: " << std::fixed << std::setprecision(3) << m.peakScore
                      << " | PNR: " << std::setprecision(1) << m.peakToNoiseRatio
                      << " | micPeak: " << std::setprecision(4) << m.maxMicAmplitude;
            if (m.secondaryPeakScore > 0.05f) {
                std::cout << " | secPeak: " << std::setprecision(2) << m.secondaryPeakScore;
            }
            std::cout << ")\n";
        }
        std::cout << std::flush;
    };

    std::cout << "Executing acoustic calibration runs...\n";
    auto result = calibrator.calibrate(*outputDev, *micDev, progressCallback);

    std::cout << "\nCalibration Results:\n";
    std::cout << "--------------------\n";
    std::cout << "  Valid Runs:        " << result.validRuns << " / " << result.totalRuns
              << " (Required >= " << config.minValidRuns << ")\n";
    if (result.validRuns > 0) {
        std::cout << "  Measurement Type:  " << result.toRecord().measurementType << "\n";
        std::cout << "  Median Arrival:    " << std::fixed << std::setprecision(2) << result.medianLatencyMs << " ms\n";
        std::cout << "  Mean Arrival:      " << std::fixed << std::setprecision(2) << result.meanLatencyMs << " ms\n";
        std::cout << "  Std Dev:           +/- " << std::fixed << std::setprecision(2) << result.stdDevMs << " ms\n";
        std::cout << "  MAD:               +/- " << std::fixed << std::setprecision(2) << result.madMs << " ms\n";
        std::cout << "  Range:             [" << std::fixed << std::setprecision(2) << result.minLatencyMs << " ms .. " << result.maxLatencyMs << " ms]\n";
        std::cout << "  Average Confidence:" << std::fixed << std::setprecision(1) << (result.averageConfidence * 100.0f) << "%\n";
        std::cout << "  Status:            " << (result.isValid ? "VALID & REPEATABLE" : "REJECTED (" + result.validationMessage + ")") << "\n\n";

        if (saveToStore && result.isValid) {
            CalibrationStore store;
            auto rec = result.toRecord();
            if (store.save(rec)) {
                std::cout << "Acoustic arrival offset saved to: " << store.storagePath() << "\n";
                std::cout << "This arrival offset (" << std::fixed << std::setprecision(2) << result.medianLatencyMs
                          << " ms) will now be automatically applied in multi-device alignment.\n\n";
            } else {
                std::cerr << "Warning: Failed to save calibration record to storage.\n\n";
            }
        } else if (!result.isValid) {
            std::cout << "  CALIBRATION REJECTED: Not saved to storage due to validation failure.\n";
            std::cout << "  Reason: " << result.validationMessage << "\n\n";
        }
    } else {
        std::cout << "  STATUS: FAILURE (No valid acoustic correlations detected)\n";
        std::cout << "  Troubleshooting:\n";
        std::cout << "    - Ensure the microphone is unmuted and near the speaker\n";
        std::cout << "    - Slightly increase volume (e.g. --volume 0.35)\n";
        std::cout << "    - Check ambient background noise\n\n";
        return 1;
    }

    return result.isValid ? 0 : 1;
}

struct OffsetStats {
    double mean = 0.0;
    double median = 0.0;
    double stdDev = 0.0;
    double minVal = 0.0;
    double maxVal = 0.0;
    double worstAbs = 0.0;
};

static OffsetStats computeOffsetStats(std::vector<double> deltas) {
    OffsetStats s;
    if (deltas.empty()) return s;
    std::sort(deltas.begin(), deltas.end());
    double sum = 0.0;
    double worst = 0.0;
    for (double v : deltas) {
        sum += v;
        worst = std::max(worst, std::abs(v));
    }
    s.mean = sum / static_cast<double>(deltas.size());
    size_t n = deltas.size();
    if (n % 2 == 1) {
        s.median = deltas[n / 2];
    } else {
        s.median = 0.5 * (deltas[n / 2 - 1] + deltas[n / 2]);
    }
    s.minVal = deltas.front();
    s.maxVal = deltas.back();
    s.worstAbs = worst;

    if (n > 1) {
        double sq = 0.0;
        for (double v : deltas) {
            double d = v - s.mean;
            sq += d * d;
        }
        s.stdDev = std::sqrt(sq / static_cast<double>(n - 1));
    } else {
        s.stdDev = 0.0;
    }
    return s;
}

int CommandInterface::handleAcousticVerifyCommand(const std::vector<std::string>& args) {
    std::vector<std::string> outputSpecs;
    std::string micSelector;
    int runs = 5;
    double delayMs = 24.28; // intentional delay applied to Output A: 131.68 - 107.40 = 24.28 ms
    double chirpDuration = 0.150;
    double volume = 0.25;

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--help" || args[i] == "-h") {
            std::cout << "\nUsage: syncwave acoustic-verify [options]\n\n"
                      << "Performs two-device acoustic verification (pre-compensation vs post-compensation)\n"
                      << "measuring the actual microphone arrival of acoustic wavefronts.\n\n"
                      << "Options:\n"
                      << "  --outputs, -O <devA,devB>     Two target output endpoints (default: '3,2')\n"
                      << "  --microphone, -m <id/index>   Reference microphone (default: system default)\n"
                      << "  --runs, -r <n>                Number of sweeps per device per experiment (default: 5)\n"
                      << "  --delay <ms>                  Intentional compensation delay applied to device A (default: 24.28)\n"
                      << "  --chirp-duration <sec>        Chirp duration in seconds (default: 0.150)\n"
                      << "  --volume, -v <amplitude>      Chirp amplitude 0.05 - 0.50 (default: 0.25)\n\n";
            return 0;
        } else if ((args[i] == "--outputs" || args[i] == "-O") && i + 1 < args.size()) {
            outputSpecs = splitString(args[++i], ',');
        } else if ((args[i] == "--microphone" || args[i] == "-m" || args[i] == "-c") && i + 1 < args.size()) {
            micSelector = args[++i];
        } else if ((args[i] == "--runs" || args[i] == "-r" || args[i] == "-n") && i + 1 < args.size()) {
            try { runs = std::stoi(args[++i]); } catch (...) {}
        } else if (args[i] == "--delay" && i + 1 < args.size()) {
            try { delayMs = std::stod(args[++i]); } catch (...) {}
        } else if (args[i] == "--chirp-duration" && i + 1 < args.size()) {
            try { chirpDuration = std::stod(args[++i]); } catch (...) {}
        } else if ((args[i] == "--volume" || args[i] == "-v") && i + 1 < args.size()) {
            try { volume = std::stod(args[++i]); } catch (...) {}
        }
    }

    if (volume > 0.50) volume = 0.50;
    if (volume < 0.05) volume = 0.05;
    if (runs < 1) runs = 5;

    auto activeDevices = deviceManager_->enumerateDevices(true);
    if (activeDevices.empty()) {
        std::cerr << "Error: No active audio output endpoints found.\n";
        return 1;
    }

    std::vector<AudioDevice> targets;
    if (outputSpecs.empty()) {
        std::optional<AudioDevice> devRealtek, devBuds;
        for (const auto& d : activeDevices) {
            if (d.name.find("Realtek") != std::string::npos) devRealtek = d;
            if (d.name.find("Buds") != std::string::npos || d.name.find("Headphones") != std::string::npos) devBuds = d;
        }
        if (devRealtek && devBuds) {
            targets.push_back(*devRealtek);
            targets.push_back(*devBuds);
        } else if (activeDevices.size() >= 2) {
            targets.push_back(activeDevices[0]);
            targets.push_back(activeDevices[1]);
        }
    } else {
        for (const auto& spec : outputSpecs) {
            try {
                size_t idx = std::stoul(spec);
                if (idx < activeDevices.size()) {
                    targets.push_back(activeDevices[idx]);
                    continue;
                }
            } catch (...) {}
            auto dev = deviceManager_->getDeviceById(spec);
            if (dev) {
                targets.push_back(*dev);
            } else {
                std::cerr << "Error: Unknown output device: " << spec << "\n";
                return 1;
            }
        }
    }

    if (targets.size() < 2) {
        std::cerr << "Error: Please specify at least 2 output devices for acoustic verification.\n";
        return 1;
    }

    std::optional<AudioDevice> micDev;
    if (micSelector.empty()) {
        micDev = deviceManager_->getDefaultCaptureDevice();
    } else {
        bool isIndex = std::all_of(micSelector.begin(), micSelector.end(), ::isdigit);
        if (isIndex) {
            micDev = deviceManager_->getCaptureDeviceByIndex(std::stoul(micSelector), true);
            if (!micDev) micDev = deviceManager_->getCaptureDeviceByIndex(std::stoul(micSelector), false);
        } else {
            micDev = deviceManager_->getCaptureDeviceById(micSelector);
        }
    }

    if (!micDev) {
        std::cerr << "Error: Target microphone not found.\n";
        return 1;
    }

    const auto& devA = targets[0]; // Realtek
    const auto& devB = targets[1]; // Buds

    std::cout << "\nSyncWave Measured Two-Device Acoustic Verification (M11 Final)\n";
    std::cout << "==============================================================\n\n";
    std::cout << "Physical Setup Parameters (held constant across all measurements):\n";
    std::cout << "  Output A:          " << devA.name << " (ID: " << devA.id << ")\n";
    std::cout << "  Output B:          " << devB.name << " (ID: " << devB.id << ")\n";
    std::cout << "  Microphone:        " << micDev->name << " (ID: " << micDev->id << ")\n";
    std::cout << "  Calibration Signal:300 Hz -> 8000 Hz linear chirp (" << (chirpDuration * 1000.0) << " ms, vol: " << volume << ")\n";
    std::cout << "  Repetitions:       " << runs << " runs per device per experiment\n";
    std::cout << "  Compensated Delay: " << std::fixed << std::setprecision(2) << delayMs << " ms on Output A (0.00 ms on Output B)\n\n";

    auto progressCallback = [](const std::string& prefix) {
        return [prefix](uint32_t cur, uint32_t total, const SingleRunMeasurement& m) {
            std::cout << "  [" << prefix << " " << cur << "/" << total << "] ";
            if (m.isSuccess) {
                std::cout << "Arrival: " << std::fixed << std::setprecision(2) << m.measuredLatencyMs << " ms"
                          << " | score: " << std::setprecision(2) << m.peakScore
                          << " | PNR: " << std::setprecision(1) << m.peakToNoiseRatio
                          << " | conf: " << std::setprecision(1) << (m.confidence * 100.0f) << "%\n";
            } else {
                std::cout << "Rejected: " << (m.failureReason.empty() ? "Correlation failed" : m.failureReason)
                          << " (score: " << std::fixed << std::setprecision(3) << m.peakScore
                          << " | PNR: " << std::setprecision(1) << m.peakToNoiseRatio << ")\n";
            }
            std::cout << std::flush;
        };
    };

    // =========================================================================
    // EXPERIMENT A: Pre-Compensation (Zero Intentional Delay)
    // =========================================================================
    std::cout << "=======================================================================\n";
    std::cout << " EXPERIMENT A: Pre-Compensation (No Intentional Delay)\n";
    std::cout << "=======================================================================\n";

    AcousticCalibrationConfig cfgA_devA;
    cfgA_devA.runs = static_cast<uint32_t>(runs);
    cfgA_devA.minValidRuns = 1;
    cfgA_devA.appliedDelayMs = 0.0;
    cfgA_devA.chirpParams.durationSec = chirpDuration;
    cfgA_devA.chirpParams.amplitude = static_cast<float>(volume);
    AcousticCalibrator calA_devA(cfgA_devA);

    std::cout << "Phase A1: Recording Output A (" << devA.name << ") [Intentional delay: 0.00 ms]...\n";
    auto resA_devA = calA_devA.calibrate(devA, *micDev, progressCallback("A1"));

    AcousticCalibrationConfig cfgA_devB = cfgA_devA;
    AcousticCalibrator calA_devB(cfgA_devB);

    std::cout << "\nPhase A2: Recording Output B (" << devB.name << ") [Intentional delay: 0.00 ms]...\n";
    auto resA_devB = calA_devB.calibrate(devB, *micDev, progressCallback("A2"));

    std::vector<double> arrivalsA_before;
    for (const auto& r : resA_devA.runs) { if (r.isSuccess) arrivalsA_before.push_back(r.measuredLatencyMs); }
    std::vector<double> arrivalsB_before;
    for (const auto& r : resA_devB.runs) { if (r.isSuccess) arrivalsB_before.push_back(r.measuredLatencyMs); }

    size_t countA = std::min(arrivalsA_before.size(), arrivalsB_before.size());
    std::vector<double> deltas_before;
    std::cout << "\nExperiment A Measured Run Pairs (Δt = arrival_Realtek - arrival_Buds):\n";
    for (size_t i = 0; i < countA; ++i) {
        double d = arrivalsA_before[i] - arrivalsB_before[i];
        deltas_before.push_back(d);
        std::cout << "  Run " << (i + 1) << ": Realtek = " << std::fixed << std::setprecision(2) << arrivalsA_before[i]
                  << " ms, Buds = " << arrivalsB_before[i]
                  << " ms  =>  Δt = " << std::showpos << d << " ms" << std::noshowpos << "\n";
    }
    if (deltas_before.empty()) {
        deltas_before.push_back(resA_devA.medianLatencyMs - resA_devB.medianLatencyMs);
    }
    auto stats_before = computeOffsetStats(deltas_before);

    // =========================================================================
    // EXPERIMENT B: Post-Compensation (Applied Delay: delayMs on Output A)
    // =========================================================================
    std::cout << "\n=======================================================================\n";
    std::cout << " EXPERIMENT B: Post-Compensation (Applied Delay: " << std::fixed << std::setprecision(2) << delayMs << " ms on Output A)\n";
    std::cout << "=======================================================================\n";

    AcousticCalibrationConfig cfgB_devA;
    cfgB_devA.runs = static_cast<uint32_t>(runs);
    cfgB_devA.minValidRuns = 1;
    cfgB_devA.appliedDelayMs = delayMs;
    cfgB_devA.chirpParams.durationSec = chirpDuration;
    cfgB_devA.chirpParams.amplitude = static_cast<float>(volume);
    AcousticCalibrator calB_devA(cfgB_devA);

    std::cout << "Phase B1: Recording Output A (" << devA.name << ") [Intentional delay: " << delayMs << " ms via DelayBuffer]...\n";
    auto resB_devA = calB_devA.calibrate(devA, *micDev, progressCallback("B1"));

    AcousticCalibrationConfig cfgB_devB = cfgB_devA;
    cfgB_devB.appliedDelayMs = 0.0;
    AcousticCalibrator calB_devB(cfgB_devB);

    std::cout << "\nPhase B2: Recording Output B (" << devB.name << ") [Intentional delay: 0.00 ms]...\n";
    auto resB_devB = calB_devB.calibrate(devB, *micDev, progressCallback("B2"));

    std::vector<double> arrivalsA_after;
    for (const auto& r : resB_devA.runs) { if (r.isSuccess) arrivalsA_after.push_back(r.measuredLatencyMs); }
    std::vector<double> arrivalsB_after;
    for (const auto& r : resB_devB.runs) { if (r.isSuccess) arrivalsB_after.push_back(r.measuredLatencyMs); }

    size_t countB = std::min(arrivalsA_after.size(), arrivalsB_after.size());
    std::vector<double> deltas_after;
    std::cout << "\nExperiment B Measured Run Pairs (Δt_after = arrival_Realtek_after - arrival_Buds_after):\n";
    for (size_t i = 0; i < countB; ++i) {
        double d = arrivalsA_after[i] - arrivalsB_after[i];
        deltas_after.push_back(d);
        std::cout << "  Run " << (i + 1) << ": Realtek(after) = " << std::fixed << std::setprecision(2) << arrivalsA_after[i]
                  << " ms, Buds(after) = " << arrivalsB_after[i]
                  << " ms  =>  Δt_after = " << std::showpos << d << " ms" << std::noshowpos << "\n";
    }
    if (deltas_after.empty()) {
        deltas_after.push_back(resB_devA.medianLatencyMs - resB_devB.medianLatencyMs);
    }
    auto stats_after = computeOffsetStats(deltas_after);

    double improvementMs = std::abs(stats_before.median) - std::abs(stats_after.median);

    // =========================================================================
    // FINAL REPORT TABLE
    // =========================================================================
    std::cout << "\n================================================================\n";
    std::cout << "             FINAL M11 ACOUSTIC VERIFICATION TABLE              \n";
    std::cout << "================================================================\n";
    std::cout << "  Metric                        Before             After        \n";
    std::cout << "  --------------------------------------------------------------\n";
    std::cout << "  Mean acoustic offset        " << std::setw(8) << std::showpos << std::fixed << std::setprecision(2)
              << stats_before.mean << " ms     " << std::setw(8) << stats_after.mean << " ms\n";
    std::cout << "  Median offset               " << std::setw(8) << stats_before.median << " ms     "
              << std::setw(8) << stats_after.median << " ms\n" << std::noshowpos;
    std::cout << "  Std deviation                 " << std::setw(6) << std::fixed << std::setprecision(2)
              << stats_before.stdDev << " ms       " << std::setw(6) << stats_after.stdDev << " ms\n";
    std::cout << "  Worst absolute offset         " << std::setw(6) << stats_before.worstAbs << " ms       "
              << std::setw(6) << stats_after.worstAbs << " ms\n";
    std::cout << "  Improvement                                    " << std::setw(6) << std::showpos
              << improvementMs << " ms\n" << std::noshowpos;
    std::cout << "  --------------------------------------------------------------\n\n";

    bool isValidated = (std::abs(stats_after.median) <= 15.0) || (improvementMs > 10.0);
    std::cout << "  M11 physical synchronization:\n";
    if (isValidated) {
        std::cout << "      VALIDATED\n";
    } else {
        std::cout << "      NOT YET VALIDATED\n";
    }
    std::cout << "================================================================\n\n";

    return 0;
}

struct SystemResourceUsage {
    double cpuPercent = 0.0;
    double memoryMb = 0.0;
};

class ResourceMonitor {
public:
    ResourceMonitor() {
        FILETIME ftCreation, ftExit;
        GetProcessTimes(GetCurrentProcess(), &ftCreation, &ftExit, &lastKernel_, &lastUser_);
        lastTime_ = std::chrono::steady_clock::now();
    }

    SystemResourceUsage sample() {
        SystemResourceUsage u;
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            u.memoryMb = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
        }

        FILETIME ftCreation, ftExit, ftKernel, ftUser;
        if (GetProcessTimes(GetCurrentProcess(), &ftCreation, &ftExit, &ftKernel, &ftUser)) {
            ULARGE_INTEGER kOld, kNew, uOld, uNew;
            kOld.LowPart = lastKernel_.dwLowDateTime; kOld.HighPart = lastKernel_.dwHighDateTime;
            kNew.LowPart = ftKernel.dwLowDateTime; kNew.HighPart = ftKernel.dwHighDateTime;
            uOld.LowPart = lastUser_.dwLowDateTime; uOld.HighPart = lastUser_.dwHighDateTime;
            uNew.LowPart = ftUser.dwLowDateTime; uNew.HighPart = ftUser.dwHighDateTime;

            uint64_t kernelDiff = kNew.QuadPart - kOld.QuadPart;
            uint64_t userDiff = uNew.QuadPart - uOld.QuadPart;
            uint64_t totalProcTime = kernelDiff + userDiff;

            auto now = std::chrono::steady_clock::now();
            double wallElapsedSec = std::chrono::duration<double>(now - lastTime_).count();
            if (wallElapsedSec > 0.0) {
                double procSec = static_cast<double>(totalProcTime) / 10000000.0;
                u.cpuPercent = (procSec / wallElapsedSec) * 100.0;
            }

            lastKernel_ = ftKernel;
            lastUser_ = ftUser;
            lastTime_ = now;
        }
        return u;
    }

private:
    FILETIME lastKernel_{}, lastUser_{};
    std::chrono::steady_clock::time_point lastTime_;
};

struct DeviceStressStats {
    std::string name;
    std::string id;
    std::vector<double> phaseErrors;
    std::vector<double> correctionsPpm;
    uint64_t wasapiUnderruns = 0;
    uint64_t queueUnderruns = 0;
    uint64_t queueOverruns = 0;

    double meanPhaseError = 0.0;
    double medianPhaseError = 0.0;
    double p95PhaseError = 0.0;
    double p99PhaseError = 0.0;
    double maxPhaseError = 0.0;
    double meanCorrection = 0.0;
    double maxCorrection = 0.0;
};

int CommandInterface::handleStressCommand(const std::vector<std::string>& args) {
    std::vector<std::string> outputSpecs;
    double durationSec = 1800.0; // default 30 min (1800s)
    std::string syncMode = "adaptive";
    std::string csvPath = "stress_telemetry.csv";
    std::string offsetsStr;
    std::string delayStr;
    double volume = 0.25;
    double frequency = 440.0;
    double disconnectTestSec = -1.0;

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--help" || args[i] == "-h") {
            std::cout << "\nUsage: syncwave stress [options]\n\n"
                      << "Runs a long-duration multi-device synchronization stress test (M12).\n\n"
                      << "Options:\n"
                      << "  --outputs, -O <id1,id2,...>     Target output endpoints\n"
                      << "  --duration, -t <sec>            Duration in seconds (default: 1800s / 30m; 3600s / 60m)\n"
                      << "  --sync <none|software|adaptive> Alignment mode (default: adaptive)\n"
                      << "  --csv, --log <path>             Path to machine-readable CSV telemetry log\n"
                      << "  --offsets <ms1,ms2,...>         Optional manual physical calibration offsets in ms\n"
                      << "  --delay <ms1,ms2,...>           Optional manual delays in ms\n"
                      << "  --volume, -v <val>              Playback volume (default: 0.25)\n"
                      << "  --quiet                         Set volume to 0.0\n"
                      << "  --disconnect-test <sec>         Trigger endpoint disconnect at specified second and reconnect after 5s\n\n";
            return 0;
        } else if ((args[i] == "--outputs" || args[i] == "-O") && i + 1 < args.size()) {
            outputSpecs = splitString(args[++i], ',');
        } else if ((args[i] == "--duration" || args[i] == "-t") && i + 1 < args.size()) {
            try { durationSec = std::stod(args[++i]); } catch (...) {}
        } else if (args[i] == "--sync" && i + 1 < args.size()) {
            syncMode = args[++i];
        } else if ((args[i] == "--csv" || args[i] == "--log") && i + 1 < args.size()) {
            csvPath = args[++i];
        } else if (args[i] == "--offsets" && i + 1 < args.size()) {
            offsetsStr = args[++i];
        } else if (args[i] == "--delay" && i + 1 < args.size()) {
            delayStr = args[++i];
        } else if ((args[i] == "--volume" || args[i] == "-v") && i + 1 < args.size()) {
            try { volume = std::stod(args[++i]); } catch (...) {}
        } else if (args[i] == "--quiet") {
            volume = 0.0;
        } else if (args[i] == "--disconnect-test" && i + 1 < args.size()) {
            try { disconnectTestSec = std::stod(args[++i]); } catch (...) {}
        }
    }

    auto activeDevices = deviceManager_->enumerateDevices(true);
    if (activeDevices.empty()) {
        std::cerr << "Error: No active audio output endpoints found.\n";
        return 1;
    }

    std::vector<AudioDevice> targetDevs;
    if (outputSpecs.empty()) {
        std::optional<AudioDevice> devRealtek, devBuds;
        for (const auto& d : activeDevices) {
            if (d.name.find("Realtek") != std::string::npos) devRealtek = d;
            if (d.name.find("Buds") != std::string::npos || d.name.find("Headphones") != std::string::npos) devBuds = d;
        }
        if (devRealtek && devBuds) {
            targetDevs.push_back(*devRealtek);
            targetDevs.push_back(*devBuds);
        } else if (activeDevices.size() >= 2) {
            targetDevs.push_back(activeDevices[0]);
            targetDevs.push_back(activeDevices[1]);
        } else {
            targetDevs.push_back(activeDevices[0]);
        }
    } else {
        for (const auto& spec : outputSpecs) {
            try {
                size_t idx = std::stoul(spec);
                if (idx < activeDevices.size()) {
                    targetDevs.push_back(activeDevices[idx]);
                    continue;
                }
            } catch (...) {}
            auto dev = deviceManager_->getDeviceById(spec);
            if (dev) {
                targetDevs.push_back(*dev);
            } else {
                std::cerr << "Error: Unknown output device: " << spec << "\n";
                return 1;
            }
        }
    }

    if (targetDevs.empty()) {
        std::cerr << "Error: No valid target devices resolved for stress test.\n";
        return 1;
    }

    std::cout << "\n================================================================================\n";
    std::cout << "           SYNCWAVE M12 LONG-DURATION SYNCHRONIZATION STRESS TEST               \n";
    std::cout << "================================================================================\n\n";
    std::cout << "Configuration Parameters:\n";
    std::cout << "  Target Endpoints (" << targetDevs.size() << "):\n";
    for (size_t i = 0; i < targetDevs.size(); ++i) {
        std::cout << "    [" << i << "] " << targetDevs[i].name << " (ID: " << targetDevs[i].id << ")\n";
    }
    std::cout << "  Duration:          " << durationSec << " seconds (" << (durationSec / 60.0) << " minutes)\n";
    std::cout << "  Sync Mode:         " << syncMode << "\n";
    std::cout << "  Telemetry CSV:     " << csvPath << "\n";
    if (disconnectTestSec > 0.0) {
        std::cout << "  Disconnect Test:   Simulating endpoint disconnect at " << disconnectTestSec << "s (5s duration)\n";
    }
    std::cout << "  Playback Signal:   " << frequency << " Hz tone (vol: " << volume << ")\n\n";

    if (!delayStr.empty()) {
        auto delays = parseDoubleList(delayStr);
        audioEngine_->setManualDelays(delays);
    }
    if (!offsetsStr.empty()) {
        auto offsets = parseDoubleList(offsetsStr);
        audioEngine_->setCalibrationOffsets(offsets);
    }

    if (syncMode == "adaptive" || syncMode == "drift") {
        audioEngine_->alignSoftwareLatencies();
        audioEngine_->enableDriftCorrection(true);
    } else if (syncMode == "software" || syncMode == "auto") {
        audioEngine_->alignSoftwareLatencies();
        audioEngine_->enableDriftCorrection(false);
    } else {
        audioEngine_->setManualDelays({});
        audioEngine_->enableDriftCorrection(false);
    }

    std::atomic<uint32_t> reconnectCount{0};
    deviceManager_->startMonitoring([&](const DeviceEvent& ev) {
        if (ev.type == DeviceEventType::Removed || 
           (ev.type == DeviceEventType::StateChanged && ev.newState != DeviceState::Active)) {
            audioEngine_->onDeviceDisconnected(ev.deviceId);
        } else if (ev.type == DeviceEventType::Added || 
                  (ev.type == DeviceEventType::StateChanged && ev.newState == DeviceState::Active)) {
            if (audioEngine_->onDeviceReconnected(ev.deviceId)) {
                reconnectCount.fetch_add(1);
            }
        }
    });

    std::ofstream csvFile(csvPath);
    if (csvFile.is_open()) {
        csvFile << "timestamp_sec,master_frame,device_index,device_id,device_name,device_frame,"
                << "queue_depth,wasapi_padding,estimated_rate_hz,estimated_ppm,filtered_ppm,"
                << "phase_error_ms,controller_ppm,resampler_ppm,underruns,overruns,device_state,"
                << "cpu_percent,memory_mb\n";
    }

    ToneParameters params{ frequency, volume, 0.0 };
    g_stopRequested.store(false);
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    if (!audioEngine_->startTone(targetDevs, params)) {
        std::cerr << "Error: Failed to start multi-output playback engine.\n";
        deviceManager_->stopMonitoring();
        SetConsoleCtrlHandler(consoleCtrlHandler, FALSE);
        return 1;
    }

    ResourceMonitor resMonitor;
    auto initialRes = resMonitor.sample();
    double memStartMb = initialRes.memoryMb;
    double memPeakMb = memStartMb;
    std::vector<double> cpuSamples;

    std::vector<DeviceStressStats> devStats(targetDevs.size());
    for (size_t i = 0; i < targetDevs.size(); ++i) {
        devStats[i].name = targetDevs[i].name;
        devStats[i].id = targetDevs[i].id;
    }

    auto startTime = std::chrono::steady_clock::now();
    auto lastSampleTime = startTime;
    auto lastSecondTime = startTime;
    auto lastPrintTime = startTime;

    bool disconnectTriggered = false;
    bool reconnectTriggered = false;

    std::cout << "Stress test running. Live status printed every 10 seconds. Press Ctrl+C to finish.\n\n" << std::flush;

    while (!g_stopRequested.load()) {
        auto now = std::chrono::steady_clock::now();
        double elapsedSec = std::chrono::duration<double>(now - startTime).count();
        if (durationSec > 0.0 && elapsedSec >= durationSec) {
            break;
        }

        // Handle disconnect test trigger if configured
        if (disconnectTestSec > 0.0 && targetDevs.size() >= 2) {
            if (elapsedSec >= disconnectTestSec && !disconnectTriggered) {
                std::cout << "\n[TEST TRIGGER] Disconnecting endpoint: " << targetDevs[1].name << "...\n" << std::flush;
                audioEngine_->onDeviceDisconnected(targetDevs[1].id);
                disconnectTriggered = true;
            } else if (elapsedSec >= disconnectTestSec + 5.0 && !reconnectTriggered) {
                std::cout << "[TEST TRIGGER] Reconnecting endpoint: " << targetDevs[1].name << "...\n" << std::flush;
                if (audioEngine_->onDeviceReconnected(targetDevs[1].id)) {
                    reconnectCount.fetch_add(1);
                    std::cout << "[TEST TRIGGER] Reconnected successfully.\n\n" << std::flush;
                } else {
                    std::cout << "[TEST TRIGGER] Reconnect failed.\n\n" << std::flush;
                }
                reconnectTriggered = true;
            }
        }

        // 10 Hz Clock sampling
        if (now - lastSampleTime >= std::chrono::milliseconds(100)) {
            audioEngine_->sampleClocks();
            lastSampleTime = now;
        }

        // 1 Hz Metrics logging & statistics accumulation
        if (now - lastSecondTime >= std::chrono::seconds(1)) {
            lastSecondTime = now;
            auto res = resMonitor.sample();
            cpuSamples.push_back(res.cpuPercent);
            memPeakMb = std::max(memPeakMb, res.memoryMb);

            auto outputs = audioEngine_->outputRouter().getOutputTelemetry();
            uint64_t masterFrames = audioEngine_->outputRouter().totalFramesDistributed();

            for (size_t i = 0; i < outputs.size() && i < devStats.size(); ++i) {
                const auto& out = outputs[i];
                devStats[i].phaseErrors.push_back(std::abs(out.filteredPhaseErrorMs));
                devStats[i].correctionsPpm.push_back(std::abs(out.currentRateAdjustmentPpm));
                devStats[i].wasapiUnderruns = out.wasapiUnderruns;
                devStats[i].queueUnderruns = out.queueUnderruns;
                devStats[i].queueOverruns = out.queueOverruns;

                if (csvFile.is_open()) {
                    csvFile << std::fixed << std::setprecision(2) << elapsedSec << ","
                            << masterFrames << ","
                            << i << ",\""
                            << out.deviceId << "\",\""
                            << out.deviceName << "\","
                            << out.clockPosition << ","
                            << out.queueAvailable << ","
                            << out.currentPadding << ","
                            << std::setprecision(2) << out.estimatedClockRateHz << ","
                            << out.rawDriftPpm << ","
                            << out.filteredDriftPpm << ","
                            << out.filteredPhaseErrorMs << ","
                            << out.commandedPpm << ","
                            << out.currentRateAdjustmentPpm << ","
                            << (out.wasapiUnderruns + out.queueUnderruns) << ","
                            << out.queueOverruns << ",\""
                            << driftCorrectionStateToString(out.driftState) << "\","
                            << res.cpuPercent << ","
                            << res.memoryMb << "\n";
                }
            }
            if (csvFile.is_open()) csvFile.flush();
        }

        // 10s Status print
        if (now - lastPrintTime >= std::chrono::seconds(10)) {
            lastPrintTime = now;
            auto outputs = audioEngine_->outputRouter().getOutputTelemetry();
            auto latestRes = resMonitor.sample();

            std::cout << "[" << std::setw(5) << std::fixed << std::setprecision(0) << elapsedSec << "s / "
                      << std::setprecision(0) << durationSec << "s] "
                      << "CPU: " << std::setprecision(1) << latestRes.cpuPercent << "% | "
                      << "Mem: " << std::setprecision(1) << latestRes.memoryMb << " MB";
            for (size_t i = 0; i < outputs.size(); ++i) {
                const auto& out = outputs[i];
                std::cout << " | [" << i << "]: err=" << std::showpos << std::fixed << std::setprecision(2)
                          << out.filteredPhaseErrorMs << "ms adj=" << out.currentRateAdjustmentPpm << "ppm" << std::noshowpos;
            }
            std::cout << "\n" << std::flush;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (csvFile.is_open()) {
        csvFile.close();
    }

    audioEngine_->stop();
    deviceManager_->stopMonitoring();
    SetConsoleCtrlHandler(consoleCtrlHandler, FALSE);

    auto finalRes = resMonitor.sample();
    double memEndMb = finalRes.memoryMb;
    double memGrowthMb = memEndMb - memStartMb;

    double cpuSum = 0.0, cpuPeak = 0.0;
    for (double c : cpuSamples) {
        cpuSum += c;
        cpuPeak = std::max(cpuPeak, c);
    }
    double cpuMean = cpuSamples.empty() ? 0.0 : (cpuSum / cpuSamples.size());

    // Compute stats per device
    for (auto& s : devStats) {
        if (!s.phaseErrors.empty()) {
            double sumErr = 0.0;
            for (double e : s.phaseErrors) { sumErr += e; }
            s.meanPhaseError = sumErr / s.phaseErrors.size();
            auto sorted = s.phaseErrors;
            std::sort(sorted.begin(), sorted.end());
            s.medianPhaseError = sorted[sorted.size() / 2];
            s.p95PhaseError = sorted[static_cast<size_t>(sorted.size() * 0.95)];
            s.p99PhaseError = sorted[static_cast<size_t>(sorted.size() * 0.99)];
            s.maxPhaseError = sorted.back();
        }
        if (!s.correctionsPpm.empty()) {
            double sumAdj = 0.0;
            double maxAdj = 0.0;
            for (double c : s.correctionsPpm) { sumAdj += c; maxAdj = std::max(maxAdj, c); }
            s.meanCorrection = sumAdj / s.correctionsPpm.size();
            s.maxCorrection = maxAdj;
        }
    }

    auto nowEnd = std::chrono::steady_clock::now();
    double actualDurationSec = std::chrono::duration<double>(nowEnd - startTime).count();

    // Print summary report
    std::cout << "\n================================================================================\n";
    std::cout << "           SYNCWAVE M12 LONG-DURATION SYNCHRONIZATION STRESS REPORT             \n";
    std::cout << "================================================================================\n\n";
    std::cout << "Configuration Summary:\n";
    std::cout << "  Duration:               " << std::fixed << std::setprecision(1) << actualDurationSec 
              << " s (" << (actualDurationSec / 60.0) << " minutes)\n";
    std::cout << "  Sync Mode:              " << syncMode << "\n";
    std::cout << "  Active Outputs:         " << targetDevs.size() << "\n";
    std::cout << "  Telemetry Log:          " << csvPath << "\n\n";

    std::cout << "Per-Device Long-Duration Synchronization Summary:\n";
    std::cout << "--------------------------------------------------------------------------------\n";
    std::cout << "Metric                         ";
    for (size_t i = 0; i < devStats.size(); ++i) {
        std::cout << "Output [" << i << "]         ";
    }
    std::cout << "\n--------------------------------------------------------------------------------\n";

    std::cout << "Device Name                    ";
    for (size_t i = 0; i < devStats.size(); ++i) {
        std::string n = devStats[i].name.substr(0, 18);
        std::cout << std::left << std::setw(20) << n << std::right;
    }
    std::cout << "\n";

    std::cout << "Mean Phase Error               ";
    for (size_t i = 0; i < devStats.size(); ++i) {
        std::cout << std::setw(8) << std::fixed << std::setprecision(2) << devStats[i].meanPhaseError << " ms            ";
    }
    std::cout << "\n";

    std::cout << "Median Phase Error             ";
    for (size_t i = 0; i < devStats.size(); ++i) {
        std::cout << std::setw(8) << devStats[i].medianPhaseError << " ms            ";
    }
    std::cout << "\n";

    std::cout << "P95 Phase Error                ";
    for (size_t i = 0; i < devStats.size(); ++i) {
        std::cout << std::setw(8) << devStats[i].p95PhaseError << " ms            ";
    }
    std::cout << "\n";

    std::cout << "P99 Phase Error                ";
    for (size_t i = 0; i < devStats.size(); ++i) {
        std::cout << std::setw(8) << devStats[i].p99PhaseError << " ms            ";
    }
    std::cout << "\n";

    std::cout << "Max Absolute Phase Error       ";
    for (size_t i = 0; i < devStats.size(); ++i) {
        std::cout << std::setw(8) << devStats[i].maxPhaseError << " ms            ";
    }
    std::cout << "\n";

    std::cout << "Mean Resampler Adjustment      ";
    for (size_t i = 0; i < devStats.size(); ++i) {
        std::cout << std::setw(8) << std::setprecision(1) << devStats[i].meanCorrection << " ppm           ";
    }
    std::cout << "\n";

    std::cout << "Max Resampler Adjustment       ";
    for (size_t i = 0; i < devStats.size(); ++i) {
        std::cout << std::setw(8) << devStats[i].maxCorrection << " ppm           ";
    }
    std::cout << "\n";

    std::cout << "WASAPI Underruns               ";
    for (size_t i = 0; i < devStats.size(); ++i) {
        std::cout << std::setw(8) << devStats[i].wasapiUnderruns << "               ";
    }
    std::cout << "\n";

    std::cout << "Queue Underruns                ";
    for (size_t i = 0; i < devStats.size(); ++i) {
        std::cout << std::setw(8) << devStats[i].queueUnderruns << "               ";
    }
    std::cout << "\n";

    std::cout << "Queue Overruns                 ";
    for (size_t i = 0; i < devStats.size(); ++i) {
        std::cout << std::setw(8) << devStats[i].queueOverruns << "               ";
    }
    std::cout << "\n--------------------------------------------------------------------------------\n\n";

    std::cout << "System Resource Telemetry:\n";
    std::cout << "  Mean Process CPU:       " << std::fixed << std::setprecision(1) << cpuMean << " %\n";
    std::cout << "  Peak Process CPU:       " << cpuPeak << " %\n";
    std::cout << "  Memory Start:           " << memStartMb << " MB\n";
    std::cout << "  Memory End:             " << memEndMb << " MB\n";
    std::cout << "  Memory Growth:          " << std::showpos << memGrowthMb << " MB\n" << std::noshowpos;
    std::cout << "  Reconnect Recoveries:   " << reconnectCount.load() << "\n\n";

    std::cout << "================================================================================\n\n";
    return 0;
}

int CommandInterface::handleCalibrationCommand(const std::vector<std::string>& args) {
    if (!args.empty() && (args[0] == "--help" || args[0] == "-h")) {
        std::cout << "\nUsage: syncwave calibration <action> [options]\n\n"
                  << "Manages persisted acoustic calibration records.\n\n"
                  << "Actions:\n"
                  << "  list                          List all stored device calibrations (default)\n"
                  << "  clear [--device <id>]         Clear all calibrations or a specific device\n\n";
        return 0;
    }

    std::string sub = args.empty() ? "list" : args[0];

    CalibrationStore store;

    if (sub == "list" || sub == "ls") {
        auto records = store.list();
        std::cout << "\nSyncWave Persisted Acoustic Calibrations\n";
        std::cout << "=======================================\n";
        std::cout << "Storage path: " << store.storagePath() << "\n\n";

        if (records.empty()) {
            std::cout << "  (No acoustic calibrations stored. Run 'syncwave acoustic-calibrate --device <id>' to calibrate a device.)\n\n";
            return 0;
        }

        for (size_t i = 0; i < records.size(); ++i) {
            const auto& r = records[i];
            std::cout << "  [" << i << "] " << r.deviceName << "\n";
            std::cout << "      Device ID:       " << r.deviceId << "\n";
            std::cout << "      Microphone:      " << (r.microphoneName.empty() ? "(Default)" : r.microphoneName) << "\n";
            std::cout << "      Type:            " << (r.measurementType.empty() ? "end_to_end_acoustic_arrival" : r.measurementType) << "\n";
            std::cout << "      Acoustic Latency:" << std::fixed << std::setprecision(2) << r.measuredLatencyMs << " ms (std: +/- " << r.uncertaintyMs << " ms, MAD: +/- " << r.madMs << " ms)\n";
            std::cout << "      Confidence:      " << std::fixed << std::setprecision(1) << (r.confidence * 100.0f) << "% (" << r.validRunCount << "/" << r.runCount << " runs)\n";
            std::cout << "      Calibrated At:   " << r.timestamp << "\n\n";
        }
        return 0;
    } else if (sub == "clear" || sub == "remove" || sub == "rm") {
        std::string devId;
        for (size_t i = 1; i < args.size(); ++i) {
            if ((args[i] == "--device" || args[i] == "-d") && i + 1 < args.size()) {
                devId = args[++i];
            }
        }

        if (devId.empty()) {
            store.clear();
            std::cout << "Cleared all persisted acoustic calibration records.\n\n";
        } else {
            if (store.remove(devId)) {
                std::cout << "Removed calibration for device: " << devId << "\n\n";
            } else {
                std::cerr << "No calibration record found for device: " << devId << "\n\n";
                return 1;
            }
        }
        return 0;
    } else {
        std::cerr << "Unknown calibration action: '" << sub << "'. Supported: 'list', 'clear'.\n";
        return 1;
    }
}

int CommandInterface::handleMediaCommand(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Error: Media file path or URL required.\nUsage: syncwave media <file> [options]\n";
        return 1;
    }

    std::string mediaPath = args[0];
    std::vector<std::string> deviceSelectors;
    std::string delayStr;
    std::string syncMode = "adaptive";
    std::string offsetsStr;
    double durationLimitSec = 0.0;
    std::string csvPath = "media_av_telemetry.csv";

    for (size_t i = 1; i < args.size(); ++i) {
        if ((args[i] == "--outputs" || args[i] == "-O") && i + 1 < args.size()) {
            auto items = splitString(args[++i], ',');
            deviceSelectors.insert(deviceSelectors.end(), items.begin(), items.end());
        } else if ((args[i] == "--output" || args[i] == "-o" || args[i] == "--device" || args[i] == "-d") && i + 1 < args.size()) {
            deviceSelectors.push_back(args[++i]);
        } else if (args[i] == "--delay" && i + 1 < args.size()) {
            delayStr = args[++i];
        } else if (args[i] == "--sync" && i + 1 < args.size()) {
            syncMode = args[++i];
        } else if (args[i] == "--offsets" && i + 1 < args.size()) {
            offsetsStr = args[++i];
        } else if ((args[i] == "--duration" || args[i] == "-t") && i + 1 < args.size()) {
            try { durationLimitSec = std::stod(args[++i]); } catch (...) {}
        } else if ((args[i] == "--csv" || args[i] == "--log") && i + 1 < args.size()) {
            csvPath = args[++i];
        }
    }

    auto resolveDevice = [this](const std::string& selector) -> std::optional<AudioDevice> {
        try {
            size_t idx = std::stoul(selector);
            auto activeDevs = deviceManager_->enumerateDevices(true);
            if (idx < activeDevs.size()) {
                return activeDevs[idx];
            }
        } catch (...) {}
        auto allDevs = deviceManager_->enumerateDevices(false);
        for (const auto& dev : allDevs) {
            if (dev.id == selector || dev.name.find(selector) != std::string::npos) {
                return dev;
            }
        }
        return std::nullopt;
    };

    std::vector<AudioDevice> targetDevs;
    if (deviceSelectors.empty()) {
        auto activeDevs = deviceManager_->enumerateDevices(true);
        if (activeDevs.size() >= 2) {
            targetDevs.push_back(activeDevs[0]);
            targetDevs.push_back(activeDevs[1]);
        } else if (!activeDevs.empty()) {
            targetDevs.push_back(activeDevs[0]);
        }
    } else {
        for (const auto& sel : deviceSelectors) {
            auto devOpt = resolveDevice(sel);
            if (devOpt) {
                targetDevs.push_back(*devOpt);
            } else {
                std::cerr << "Warning: Could not resolve output endpoint '" << sel << "'\n";
            }
        }
    }

    if (targetDevs.empty()) {
        std::cerr << "Error: No valid output endpoints available for media playback.\n";
        return 1;
    }

    std::cout << "\n================================================================================\n";
    std::cout << "           SYNCWAVE M13 AUDIO/VIDEO SYNCHRONIZATION PLAYBACK ENGINE             \n";
    std::cout << "================================================================================\n\n";
    std::cout << "Media Source:        " << mediaPath << "\n";
    std::cout << "Sync Mode:           " << syncMode << "\n";
    std::cout << "Target Endpoints (" << targetDevs.size() << "):\n";
    for (size_t i = 0; i < targetDevs.size(); ++i) {
        std::cout << "  [" << i << "] " << targetDevs[i].name << " (" << targetDevs[i].id << ")\n";
    }
    std::cout << "Telemetry CSV:       " << csvPath << "\n\n";

    // 1. Initialize AudioEngine output routing
    audioEngine_->stop();
    auto& router = audioEngine_->outputRouter();
    router.clearOutputs();
    for (const auto& d : targetDevs) {
        router.addOutput(d);
    }

    // Match 48kHz stereo canonical bus
    if (!router.initializeOutputs(48000, 2)) {
        std::cerr << "Error: Failed to initialize WASAPI output endpoints.\n";
        return 1;
    }

    // Apply manual delays / calibration offsets if provided
    if (!delayStr.empty()) {
        auto delays = parseDoubleList(delayStr);
        for (size_t i = 0; i < delays.size(); ++i) {
            router.setDeviceDelayMs(i, delays[i]);
        }
    }
    if (!offsetsStr.empty()) {
        auto offsets = parseDoubleList(offsetsStr);
        for (size_t i = 0; i < offsets.size(); ++i) {
            router.setDeviceCalibrationOffsetMs(i, offsets[i]);
        }
    }

    bool useDrift = (syncMode == "adaptive" || syncMode == "software");
    router.setDriftCorrectionEnabled(useDrift);

    if (!router.startOutputs()) {
        std::cerr << "Error: Failed to start audio render endpoints.\n";
        router.closeOutputs();
        return 1;
    }

    // 2. Initialize VlcMediaEngine
    VlcMediaEngine mediaEngine(audioEngine_->masterBus(), router);
    if (!mediaEngine.load(mediaPath)) {
        std::cerr << "Error: Failed to load media file into VLC engine: " << mediaPath << "\n";
        router.stopOutputs();
        router.closeOutputs();
        return 1;
    }

    std::ofstream csvFile(csvPath);
    if (csvFile.is_open()) {
        csvFile << "timestamp_sec,media_time_ms,video_time_ms,audio_master_ms,audio_acoustic_ms,av_offset_ms,playback_state,playback_rate,cpu_percent,memory_mb\n";
    }

    g_stopRequested.store(false);
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    if (!mediaEngine.play()) {
        std::cerr << "Error: Failed to start VLC media playback.\n";
        router.stopOutputs();
        router.closeOutputs();
        SetConsoleCtrlHandler(consoleCtrlHandler, FALSE);
        return 1;
    }

    std::cout << "Media playback running. Press Ctrl+C to stop.\n\n";

    auto startTime = std::chrono::steady_clock::now();
    auto lastSecondTime = startTime;
    auto lastPrintTime = startTime;
    auto lastClockSample = startTime;

    std::vector<double> avOffsets;

    while (!g_stopRequested.load()) {
        auto now = std::chrono::steady_clock::now();
        double elapsedSec = std::chrono::duration<double>(now - startTime).count();

        if (durationLimitSec > 0.0 && elapsedSec >= durationLimitSec) {
            break;
        }

        // 10 Hz Clock sampling
        if (now - lastClockSample >= std::chrono::milliseconds(100)) {
            router.sampleAllClocks(now);
            lastClockSample = now;
        }

        // 1 Hz Telemetry & Logging
        if (now - lastSecondTime >= std::chrono::seconds(1)) {
            lastSecondTime = now;
            auto syncState = mediaEngine.getSyncState();
            avOffsets.push_back(syncState.audioVideoOffsetMs);

            if (csvFile.is_open()) {
                csvFile << std::fixed << std::setprecision(2) << elapsedSec << ","
                        << syncState.mediaPositionMs << ","
                        << syncState.videoPositionMs << ","
                        << syncState.audioMasterPositionMs << ","
                        << syncState.audioAcousticPositionMs << ","
                        << syncState.audioVideoOffsetMs << ",\""
                        << syncState.playbackState << "\","
                        << syncState.playbackRate << ",0.0,0.0\n";
                csvFile.flush();
            }
        }

        // 5s Status Print
        if (now - lastPrintTime >= std::chrono::seconds(5)) {
            lastPrintTime = now;
            auto syncState = mediaEngine.getSyncState();
            std::cout << "[" << std::setw(4) << std::fixed << std::setprecision(0) << elapsedSec << "s] "
                      << "Media: " << std::fixed << std::setprecision(0) << (syncState.mediaPositionMs / 1000.0) << "s | "
                      << "Video: " << syncState.videoPositionMs << "ms | "
                      << "Audio: " << syncState.audioMasterPositionMs << "ms | "
                      << "A/V Offset: " << std::showpos << std::fixed << std::setprecision(1) << syncState.audioVideoOffsetMs << "ms" << std::noshowpos
                      << " (" << (syncState.isSynchronized ? "LOCKED" : "CORRECTING") << ")\n" << std::flush;
        }

        if (mediaEngine.stateString() == "Ended" || mediaEngine.stateString() == "Stopped") {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (csvFile.is_open()) {
        csvFile.close();
    }

    mediaEngine.stop();
    router.stopOutputs();
    router.closeOutputs();
    SetConsoleCtrlHandler(consoleCtrlHandler, FALSE);

    std::cout << "\n================================================================================\n";
    std::cout << "           SYNCWAVE M13 A/V SYNCHRONIZATION REPORT                              \n";
    std::cout << "================================================================================\n\n";

    if (!avOffsets.empty()) {
        double sum = 0.0, maxOff = 0.0;
        for (double o : avOffsets) {
            sum += o;
            maxOff = std::max(maxOff, std::abs(o));
        }
        double meanOff = sum / avOffsets.size();
        auto sorted = avOffsets;
        std::sort(sorted.begin(), sorted.end());
        double medianOff = sorted[sorted.size() / 2];

        std::cout << "A/V Synchronization Metrics (" << avOffsets.size() << " samples):\n";
        std::cout << "  Mean A/V Offset:        " << std::showpos << std::fixed << std::setprecision(2) << meanOff << " ms\n";
        std::cout << "  Median A/V Offset:      " << medianOff << " ms\n";
        std::cout << "  Peak Absolute Offset:   " << std::noshowpos << maxOff << " ms\n";
        std::cout << "  Telemetry CSV Saved:    " << csvPath << "\n\n";
    }

    return 0;
}

int CommandInterface::run(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    if (args.empty()) {
        printHelp();
        return 0;
    }

    const std::string& cmd = args[0];
    std::vector<std::string> subArgs(args.begin() + 1, args.end());

    if (cmd == "devices" || cmd == "list") {
        return handleDevicesCommand(subArgs);
    } else if (cmd == "watch" || cmd == "monitor-devices") {
        return handleWatchCommand(subArgs);
    } else if (cmd == "tone" || cmd == "play-tone") {
        return handleToneCommand(subArgs);
    } else if (cmd == "capture" || cmd == "loopback") {
        return handleCaptureCommand(subArgs);
    } else if (cmd == "clock-test") {
        return handleClockTestCommand(subArgs);
    } else if (cmd == "latency-test") {
        return handleLatencyTestCommand(subArgs);
    } else if (cmd == "calibrate" || cmd == "align") {
        return handleCalibrateCommand(subArgs);
    } else if (cmd == "acoustic-calibrate" || cmd == "chirp-calibrate") {
        return handleAcousticCalibrateCommand(subArgs);
    } else if (cmd == "acoustic-verify" || cmd == "verify-acoustic") {
        return handleAcousticVerifyCommand(subArgs);
    } else if (cmd == "stress" || cmd == "stress-test") {
        return handleStressCommand(subArgs);
    } else if (cmd == "media" || cmd == "play-media") {
        return handleMediaCommand(subArgs);
    } else if (cmd == "calibration") {
        return handleCalibrationCommand(subArgs);
    } else if (cmd == "status" || cmd == "diag") {
        return handleStatusCommand(subArgs);
    } else if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        printHelp();
        return 0;
    } else if (cmd == "version" || cmd == "--version" || cmd == "-v") {
        printVersion();
        return 0;
    } else {
        std::cerr << "Unknown command: '" << cmd << "'\n\n";
        printHelp();
        return 1;
    }
}

} // namespace syncwave
