#pragma once

#include <vector>
#include <string>
#include <memory>

namespace syncwave {

class DeviceManager;
class AudioEngine;

class CommandInterface {
public:
    CommandInterface();
    ~CommandInterface();

    int run(int argc, char* argv[]);

private:
    void printHelp() const;
    void printVersion() const;
    int handleDevicesCommand(const std::vector<std::string>& args);
    int handleWatchCommand(const std::vector<std::string>& args);
    int handleToneCommand(const std::vector<std::string>& args);
    int handleCaptureCommand(const std::vector<std::string>& args);
    int handleClockTestCommand(const std::vector<std::string>& args);
    int handleLatencyTestCommand(const std::vector<std::string>& args);
    int handleCalibrateCommand(const std::vector<std::string>& args);
    int handleAcousticCalibrateCommand(const std::vector<std::string>& args);
    int handleAcousticVerifyCommand(const std::vector<std::string>& args);
    int handleStressCommand(const std::vector<std::string>& args);
    int handleCalibrationCommand(const std::vector<std::string>& args);
    int handleStatusCommand(const std::vector<std::string>& args);
    int handleMediaCommand(const std::vector<std::string>& args);

    std::unique_ptr<DeviceManager> deviceManager_;
    std::unique_ptr<AudioEngine> audioEngine_;
};

} // namespace syncwave
