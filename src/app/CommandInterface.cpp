#include "CommandInterface.h"
#include "../windows/DeviceManager.h"
#include "../audio/AudioEngine.h"

#include <windows.h>
#include <iostream>
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

constexpr const char* SYNCWAVE_VERSION = "0.9.0-alpha";

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
              << "  syncwave status                            Display engine, router, and master audio bus status\n"
              << "  syncwave help                              Show this help message\n"
              << "  syncwave --version                         Display version\n\n"
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
              << "  --runs, -n <count>                         Number of repeated test runs (default: 5)\n\n";
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

    auto startTime = std::chrono::steady_clock::now();
    auto lastSampleTime = startTime;

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
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
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

    if (!offsetsStr.empty()) {
        auto offsets = parseDoubleList(offsetsStr);
        for (size_t i = 0; i < models.size() && i < offsets.size(); ++i) {
            models[i].optionalCalibrationOffsetMs = offsets[i];
            models[i].recalculate();
        }
    }

    auto plan = SyncController::computeSoftwareAlignmentPlan(models);

    audioEngine_->stop();

    std::cout << "\nDevice Latency Breakdown:\n";
    std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
    std::cout << "  Idx Device Name               Rate (Hz)  WASAPI Stream  Padding (ms)  Resampler (ms)  Est. SW (ms)  Delay (ms)  Delay (frames)\n";
    std::cout << "------------------------------------------------------------------------------------------------------------------------\n";

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
                  << std::setw(14) << m.resamplerLatencyMs << "  "
                  << std::setw(12) << m.estimatedSoftwareLatencyMs << "  "
                  << std::setw(10) << delayMs << "  "
                  << std::setw(14) << delayFrames << "\n";
    }
    std::cout << "------------------------------------------------------------------------------------------------------------------------\n";
    std::cout << "Target Alignment Latency: " << std::fixed << std::setprecision(2) << plan.targetLatencyMs << " ms\n";
    std::cout << "Sync State:               " << syncStateToString(plan.syncState) << "\n\n";

    std::cout << "=======================================================================\n";
    std::cout << "               PHYSICAL / ACOUSTIC LATENCY DISCLAIMER                  \n";
    std::cout << "=======================================================================\n";
    std::cout << "1. WHAT IS COMPENSATED:\n";
    std::cout << "   Software-domain delays: WASAPI stream latency buffer, current endpoint\n";
    std::cout << "   padding, and linear-phase resampler group delay.\n";
    std::cout << "2. WHAT IS NOT COMPENSATED AUTOMATICALLY:\n";
    std::cout << "   Physical acoustic latency: Bluetooth A2DP transport packetization and\n";
    std::cout << "   RF buffer, hardware DAC reconstruction filters, amplifier/driver latency,\n";
    std::cout << "   and room acoustic propagation time.\n";
    std::cout << "3. MANUAL ACOUSTIC OFFSET CALIBRATION:\n";
    std::cout << "   To calibrate physical acoustic differences, measure arrival times with\n";
    std::cout << "   an external microphone or transient test, and supply offsets via:\n";
    std::cout << "     syncwave tone --outputs " << (outputSpecs.empty() ? "0,1" : outputSpecs[0])
              << " --offsets <off0,off1>\n\n";

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
