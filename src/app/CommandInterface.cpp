#include "CommandInterface.h"
#include "../windows/DeviceManager.h"
#include "../audio/AudioEngine.h"

#include <windows.h>
#include <iostream>
#include <iomanip>
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

constexpr const char* SYNCWAVE_VERSION = "0.5.0-alpha";

static std::atomic<bool> g_stopRequested{false};

static BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
        g_stopRequested.store(true);
        return TRUE;
    }
    return FALSE;
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
              << "  syncwave capture [options]                 Capture Windows system audio via WASAPI Loopback and route to output\n"
              << "  syncwave status                            Display engine and master audio bus status\n"
              << "  syncwave help                              Show this help message\n"
              << "  syncwave --version                         Display version\n\n"
              << "Options for 'capture':\n"
              << "  --source, -s <index|id>                    Capture endpoint (default: system default)\n"
              << "  --output, -o <index|id>                    Output endpoint (default: system default)\n"
              << "  --duration, -t <sec>                       Duration in seconds (0 = continuous, default: 5s)\n\n"
              << "Options for 'tone':\n"
              << "  --device, -d <index|id>                    Target device index or opaque ID (default: system default)\n"
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
        }
    }

    std::cout << "\nSyncWave Device Monitor\n";
    std::cout << "=======================\n\n";
    std::cout << "Watching for audio device changes (hot-plug, disconnects, defaults)...\n";
    if (timeoutSec > 0) {
        std::cout << "Running for " << timeoutSec << " second(s)...\n";
    } else {
        std::cout << "Press Ctrl+C to exit.\n";
    }
    std::cout << std::endl;

    g_stopRequested.store(false);
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::queue<DeviceEvent> eventQueue;

    bool started = deviceManager_->startMonitoring([&](const DeviceEvent& ev) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            eventQueue.push(ev);
        }
        queueCv.notify_one();
    });

    if (!started) {
        std::cerr << "Error: Failed to register device notification listener." << std::endl;
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
    std::cout << "  Format:    " << diag.masterFormat.formatString() << "\n";
    std::cout << "  Capacity:  " << diag.busCapacityFrames << " frames\n";
    std::cout << "  Available: " << diag.busAvailableFrames << " frames\n";
    std::cout << "  Produced:  " << diag.busFramesWritten << " frames\n";
    std::cout << "  Consumed:  " << diag.busFramesRead << " frames\n";
    std::cout << "  Underruns: " << diag.busUnderruns << "\n";
    std::cout << "  Overruns:  " << diag.busOverruns << "\n\n";

    if (!diag.deviceName.empty()) {
        std::cout << "Active Output:\n";
        std::cout << "  Device:          " << diag.deviceName << "\n";
        std::cout << "  State:           " << (diag.isRunning ? "RUNNING" : "STOPPED") << "\n";
        std::cout << "  Format:          " << diag.format.formatString() << "\n";
        std::cout << "  Buffer:          " << diag.bufferFrameCount << " frames\n";
        std::cout << "  Frames rendered: " << diag.framesRendered << "\n";
        std::cout << "  Underruns:       " << diag.outputUnderruns << "\n";
        std::cout << "  Clock position:  " << diag.clockPosition << " frames (@ " 
                  << diag.clockFrequency << " Hz)\n\n";
    }

    return 0;
}

int CommandInterface::handleToneCommand(const std::vector<std::string>& args) {
    std::string deviceSelector;
    double frequency = 440.0;
    double duration = 5.0;
    double volume = 0.25;

    for (size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "--device" || args[i] == "-d") && i + 1 < args.size()) {
            deviceSelector = args[++i];
        } else if ((args[i] == "--frequency" || args[i] == "-f") && i + 1 < args.size()) {
            try { frequency = std::stod(args[++i]); } catch (...) {}
        } else if ((args[i] == "--duration" || args[i] == "-t") && i + 1 < args.size()) {
            try { duration = std::stod(args[++i]); } catch (...) {}
        } else if ((args[i] == "--volume" || args[i] == "-v") && i + 1 < args.size()) {
            try { volume = std::stod(args[++i]); } catch (...) {}
        }
    }

    std::optional<AudioDevice> targetDev;
    if (deviceSelector.empty()) {
        targetDev = deviceManager_->getDefaultDevice();
        if (!targetDev) {
            std::cerr << "Error: No default audio render device found on host.\n";
            return 1;
        }
    } else {
        bool isIndex = !deviceSelector.empty() && 
                       std::all_of(deviceSelector.begin(), deviceSelector.end(), ::isdigit);
        if (isIndex) {
            size_t idx = std::stoul(deviceSelector);
            targetDev = deviceManager_->getDeviceByIndex(idx, true);
            if (!targetDev) {
                targetDev = deviceManager_->getDeviceByIndex(idx, false);
            }
        } else {
            targetDev = deviceManager_->getDeviceById(deviceSelector);
        }

        if (!targetDev) {
            std::cerr << "Error: Audio device '" << deviceSelector << "' not found.\n";
            return 1;
        }
    }

    if (!targetDev->isActive) {
        std::cerr << "Warning: Target audio device '" << targetDev->name 
                  << "' is not in Active state (" << targetDev->stateString() << ").\n";
    }

    ToneParameters params;
    params.frequencyHz = frequency;
    params.volume = volume;
    params.durationSec = duration;

    std::cout << "\nSyncWave Tone Generator (Master Audio Bus Pipeline)\n";
    std::cout << "===================================================\n\n";
    std::cout << "Device:\n";
    std::cout << "  Name:  " << targetDev->name << "\n";
    std::cout << "  ID:    " << targetDev->id << "\n\n";

    std::atomic<bool> deviceDisconnected{false};

    deviceManager_->startMonitoring([&](const DeviceEvent& ev) {
        if (ev.deviceId == targetDev->id) {
            if (ev.type == DeviceEventType::Removed || 
               (ev.type == DeviceEventType::StateChanged && ev.newState != DeviceState::Active)) {
                deviceDisconnected.store(true);
            }
        }
    });

    bool started = audioEngine_->startTone(*targetDev, params);
    if (!started) {
        std::cerr << "Error: Failed to start playback through Master Audio Bus on device '" << targetDev->name << "'.\n";
        deviceManager_->stopMonitoring();
        return 1;
    }

    auto diag = audioEngine_->getDiagnostics();

    std::cout << "Pipeline Architecture:\n";
    std::cout << "  ToneGenerator -> MasterAudioBus (RingBuffer) -> WasapiOutput -> Hardware\n\n";

    std::cout << "Master Bus:\n";
    std::cout << "  Format:      " << diag.masterFormat.formatString() << "\n";
    std::cout << "  Capacity:    " << diag.busCapacityFrames << " frames (~" 
              << (diag.busCapacityFrames * 1000 / diag.masterFormat.sampleRate) << " ms)\n\n";

    std::cout << "Hardware Output:\n";
    std::cout << "  Format:      " << diag.format.formatString() << "\n";
    std::cout << "  Buffer:      " << diag.bufferFrameCount << " frames\n\n";

    std::cout << "Tone:\n";
    std::cout << "  Frequency:   " << frequency << " Hz\n";
    std::cout << "  Volume:      " << volume << "\n";
    if (duration > 0) {
        std::cout << "  Duration:    " << duration << " second(s)\n";
    } else {
        std::cout << "  Duration:    Continuous\n";
    }

    std::cout << "\nPlayback started.\n";
    std::cout << "Press Ctrl+C to stop.\n\n" << std::flush;

    g_stopRequested.store(false);
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    auto startTime = std::chrono::steady_clock::now();

    while (!g_stopRequested.load()) {
        if (deviceDisconnected.load()) {
            std::cout << "\nAudio device became unavailable.\nPlayback stopped.\n" << std::flush;
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
    std::cout << "  Capacity:       " << finalDiag.busCapacityFrames << " frames\n";
    std::cout << "  Frames produced:" << finalDiag.busFramesWritten << "\n";
    std::cout << "  Frames consumed:" << finalDiag.busFramesRead << "\n";
    std::cout << "  Bus underruns:  " << finalDiag.busUnderruns << "\n";
    std::cout << "  Bus overruns:   " << finalDiag.busOverruns << "\n\n";

    std::cout << "Hardware Output Telemetry:\n";
    std::cout << "  Frames rendered:" << finalDiag.framesRendered << "\n";
    std::cout << "  HW underruns:   " << finalDiag.outputUnderruns << "\n";
    std::cout << "  Clock position: " << finalDiag.clockPosition << " frames (@ " 
              << finalDiag.clockFrequency << " Hz)\n\n" << std::flush;

    return 0;
}

int CommandInterface::handleCaptureCommand(const std::vector<std::string>& args) {
    std::string sourceSelector;
    std::string outputSelector;
    double duration = 5.0;

    for (size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "--source" || args[i] == "-s") && i + 1 < args.size()) {
            sourceSelector = args[++i];
        } else if ((args[i] == "--output" || args[i] == "-o") && i + 1 < args.size()) {
            outputSelector = args[++i];
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

    auto outputDev = resolveDevice(outputSelector);
    if (!outputDev) {
        std::cerr << "Error: Output destination endpoint '" << outputSelector << "' not found.\n";
        return 1;
    }

    std::cout << "\nSyncWave System Audio Capture (WASAPI Loopback Pipeline)\n";
    std::cout << "========================================================\n\n";

    std::cout << "Capture Source:\n";
    std::cout << "  Device: " << sourceDev->name << "\n";
    std::cout << "  State:  " << sourceDev->stateString() << "\n";
    std::cout << "  ID:     " << sourceDev->id << "\n\n";

    std::cout << "Output Destination:\n";
    std::cout << "  Device: " << outputDev->name << "\n";
    std::cout << "  State:  " << outputDev->stateString() << "\n";
    std::cout << "  ID:     " << outputDev->id << "\n\n";

    if (sourceDev->id == outputDev->id) {
        std::cout << "[NOTE] Capturing from and outputting to the same endpoint (" << sourceDev->name << ").\n";
        std::cout << "To avoid feedback when live audio inputs are present, using separate physical endpoints\n";
        std::cout << "is recommended (e.g., --source <Speakers> --output <Headphones>).\n\n";
    }

    std::atomic<bool> deviceDisconnected{false};
    deviceManager_->startMonitoring([&](const DeviceEvent& ev) {
        if (ev.deviceId == sourceDev->id || ev.deviceId == outputDev->id) {
            if (ev.type == DeviceEventType::Removed || 
               (ev.type == DeviceEventType::StateChanged && ev.newState != DeviceState::Active)) {
                deviceDisconnected.store(true);
            }
        }
    });

    bool started = audioEngine_->startCapture(*sourceDev, *outputDev);
    if (!started) {
        std::cerr << "Error: Failed to initialize and start loopback capture pipeline.\n";
        deviceManager_->stopMonitoring();
        return 1;
    }

    auto diag = audioEngine_->getDiagnostics();

    std::cout << "Pipeline Architecture:\n";
    std::cout << "  Windows Audio -> WASAPI Loopback Capture -> MasterAudioBus (RingBuffer)";
    if (diag.captureFormat.sampleRate != diag.format.sampleRate) {
        std::cout << " -> Resampler (" << diag.captureFormat.sampleRate << " -> " << diag.format.sampleRate << " Hz)";
    }
    std::cout << " -> WasapiOutput -> Hardware\n\n";

    std::cout << "Capture Stream:\n";
    std::cout << "  Format:      " << diag.captureFormat.formatString() << "\n\n";

    std::cout << "Master Audio Bus:\n";
    std::cout << "  Format:      " << diag.masterFormat.formatString() << "\n";
    std::cout << "  Capacity:    " << diag.busCapacityFrames << " frames (~" 
              << (diag.busCapacityFrames * 1000 / diag.masterFormat.sampleRate) << " ms)\n\n";

    std::cout << "Hardware Output:\n";
    std::cout << "  Format:      " << diag.format.formatString() << "\n";
    std::cout << "  Buffer:      " << diag.bufferFrameCount << " frames\n\n";

    if (duration > 0) {
        std::cout << "Duration:      " << duration << " second(s)\n";
    } else {
        std::cout << "Duration:      Continuous (Press Ctrl+C to stop)\n";
    }

    std::cout << "\nLoopback capture streaming active.\n";
    std::cout << "Play any audio in Windows (browser, media player, games, etc.)\n";
    std::cout << "Press Ctrl+C to stop.\n\n" << std::flush;

    g_stopRequested.store(false);
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    auto startTime = std::chrono::steady_clock::now();

    while (!g_stopRequested.load()) {
        if (deviceDisconnected.load()) {
            std::cout << "\nAudio endpoint became unavailable.\nCapture stopped.\n" << std::flush;
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
    std::cout << "  Frames captured:  " << finalDiag.framesCaptured << "\n";
    std::cout << "  Packets captured: " << finalDiag.packetsCaptured << "\n";
    std::cout << "  Silence packets:  " << finalDiag.silencePackets << "\n";
    std::cout << "  Discontinuities:  " << finalDiag.discontinuities << "\n";
    std::cout << "  Capture errors:   " << finalDiag.captureErrors << "\n\n";

    std::cout << "Master Audio Bus Telemetry:\n";
    std::cout << "  Capacity:         " << finalDiag.busCapacityFrames << " frames\n";
    std::cout << "  Frames written:   " << finalDiag.busFramesWritten << "\n";
    std::cout << "  Frames read:      " << finalDiag.busFramesRead << "\n";
    std::cout << "  Bus underruns:    " << finalDiag.busUnderruns << "\n";
    std::cout << "  Bus overruns:     " << finalDiag.busOverruns << "\n\n";

    std::cout << "Hardware Output Telemetry:\n";
    std::cout << "  Frames rendered:  " << finalDiag.framesRendered << "\n";
    std::cout << "  HW underruns:     " << finalDiag.outputUnderruns << "\n";
    std::cout << "  Clock position:   " << finalDiag.clockPosition << " frames (@ " 
              << finalDiag.clockFrequency << " Hz)\n\n" << std::flush;

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
