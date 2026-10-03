#include "CommandInterface.h"
#include "../windows/DeviceManager.h"

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <algorithm>

namespace syncwave {

constexpr const char* SYNCWAVE_VERSION = "0.1.0-alpha";

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

int CommandInterface::run(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    if (args.empty()) {
        // Default to printing help or devices
        printHelp();
        return 0;
    }

    const std::string& cmd = args[0];
    std::vector<std::string> subArgs(args.begin() + 1, args.end());

    if (cmd == "devices" || cmd == "list") {
        return handleDevicesCommand(subArgs);
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
