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

constexpr const char* SYNCWAVE_VERSION = "0.3.0-alpha";

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
              << "  syncwave tone [options]                    Play synthetic PCM sine wave on an audio device\n"
              << "  syncwave help                              Show this help message\n"
              << "  syncwave --version                         Display version\n\n"
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

int CommandInterface::handleToneCommand(const std::vector<std::string>& args) {
    std::string deviceSelector;
    double frequency = 440.0;
    double duration = 5.0; // default 5 seconds
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

    // Resolve target audio device
    std::optional<AudioDevice> targetDev;
    if (deviceSelector.empty()) {
        targetDev = deviceManager_->getDefaultDevice();
        if (!targetDev) {
            std::cerr << "Error: No default audio render device found on host.\n";
            return 1;
        }
    } else {
        // Check if selector is numeric index
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

    std::cout << "\nSyncWave Tone Generator\n";
    std::cout << "=======================\n\n";
    std::cout << "Device:\n";
    std::cout << "  Name:  " << targetDev->name << "\n";
    std::cout << "  ID:    " << targetDev->id << "\n\n";

    std::atomic<bool> deviceDisconnected{false};

    // Monitor endpoint notifications to detect disconnection during playback
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
        std::cerr << "Error: Failed to start WASAPI playback on device '" << targetDev->name << "'.\n";
        deviceManager_->stopMonitoring();
        return 1;
    }

    auto diag = audioEngine_->getDiagnostics();

    std::cout << "Format:\n";
    std::cout << "  Sample rate: " << diag.format.sampleRate << " Hz\n";
    std::cout << "  Channels:    " << diag.format.channels << "\n";
    std::cout << "  Format:      " << sampleTypeToString(diag.format.sampleType) 
              << " (" << diag.format.bitsPerSample << "-bit)\n";
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
    std::cout << "Frames rendered: " << finalDiag.framesRendered << "\n";
    std::cout << "Underruns:       " << finalDiag.underruns << "\n";
    std::cout << "Clock position:  " << finalDiag.clockPosition << " frames (@ " 
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
