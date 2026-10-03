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

constexpr const char* SYNCWAVE_VERSION = "0.6.0-alpha";

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
              << "  syncwave status                            Display engine, router, and master audio bus status\n"
              << "  syncwave help                              Show this help message\n"
              << "  syncwave --version                         Display version\n\n"
              << "Options for 'capture':\n"
              << "  --source, -s <index|id>                    Capture endpoint (default: system default)\n"
              << "  --output, -o <index|id>                    Output endpoint (can be repeated for multiple outputs)\n"
              << "  --outputs, -O <id1,id2,...>                Comma-separated list of output endpoints\n"
              << "  --duration, -t <sec>                       Duration in seconds (0 = continuous, default: 5s)\n\n"
              << "Options for 'tone':\n"
              << "  --device, -d, -o <index|id>                Target output endpoint (can be repeated for multiple outputs)\n"
              << "  --outputs, -O <id1,id2,...>                Comma-separated list of target output endpoints\n"
              << "  --frequency, -f <Hz>                       Tone frequency in Hz (default: 440 Hz)\n"
              << "  --duration, -t <sec>                       Playback duration in seconds (0 = continuous, default: 5s)\n"
              << "  --volume, -v <0.0..1.0>                    Volume amplitude level (default: 0.25)\n\n";
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

    std::cout << "Configured Outputs (" << diag.outputs.size() << "):\n";
    if (diag.outputs.empty()) {
        std::cout << "  (No outputs configured)\n\n";
    } else {
        for (size_t i = 0; i < diag.outputs.size(); ++i) {
            const auto& out = diag.outputs[i];
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
            std::cout << "      Clock position:   " << out.clockPosition << " frames (@ " 
                      << out.clockFrequency << " Hz)\n\n";
        }
    }

    return 0;
}

int CommandInterface::handleToneCommand(const std::vector<std::string>& args) {
    std::vector<std::string> deviceSelectors;
    double frequency = 440.0;
    double duration = 5.0;
    double volume = 0.25;

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

    std::cout << "Per-Output Telemetry:\n";
    for (size_t i = 0; i < finalDiag.outputs.size(); ++i) {
        const auto& out = finalDiag.outputs[i];
        std::cout << "  [" << i << "] " << out.deviceName << "\n";
        std::cout << "      Format:           " << out.format.formatString() << "\n";
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
        std::cout << "      Clock position:   " << out.clockPosition << " frames (@ " 
                  << out.clockFrequency << " Hz)\n\n";
    }
    std::cout << std::flush;

    return 0;
}

int CommandInterface::handleCaptureCommand(const std::vector<std::string>& args) {
    std::string sourceSelector;
    std::vector<std::string> outputSelectors;
    double duration = 5.0;

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

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
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

    std::cout << "Per-Output Telemetry:\n";
    for (size_t i = 0; i < finalDiag.outputs.size(); ++i) {
        const auto& out = finalDiag.outputs[i];
        std::cout << "  [" << i << "] " << out.deviceName << "\n";
        std::cout << "      Format:           " << out.format.formatString() << "\n";
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
        std::cout << "      Clock position:   " << out.clockPosition << " frames (@ " 
                  << out.clockFrequency << " Hz)\n\n";
    }
    std::cout << std::flush;

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
