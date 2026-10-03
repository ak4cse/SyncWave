#include "CommandInterface.h"
#include "../windows/DeviceManager.h"

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

namespace syncwave {

constexpr const char* SYNCWAVE_VERSION = "0.2.0-alpha";

static std::atomic<bool> g_stopRequested{false};

static BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
        g_stopRequested.store(true);
        return TRUE;
    }
    return FALSE;
}

CommandInterface::CommandInterface()
    : deviceManager_(std::make_unique<DeviceManager>()) {}

CommandInterface::~CommandInterface() = default;

void CommandInterface::printVersion() const {
    std::cout << "SyncWave v" << SYNCWAVE_VERSION << " (Windows CLI Audio Engine)\n";
}

void CommandInterface::printHelp() const {
    printVersion();
    std::cout << "\nUsage:\n"
              << "  syncwave devices [--all]   Enumerate audio output devices\n"
              << "  syncwave watch             Monitor audio endpoint changes in real time\n"
              << "  syncwave help              Show this help message\n"
              << "  syncwave --version         Display version\n\n"
              << "Options for 'devices':\n"
              << "  --all                      Include disabled, unplugged, and inactive endpoints\n\n";
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
    int timeoutSec = -1; // -1 means run until Ctrl+C
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

                // Unlock while printing to avoid holding lock during stdout
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
