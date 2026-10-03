#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <memory>
#include <functional>

namespace syncwave {

enum class DeviceState {
    Active,
    Disabled,
    NotPresent,
    Unplugged,
    Unknown
};

std::string deviceStateToString(DeviceState state);

enum class DeviceEventType {
    Added,
    Removed,
    StateChanged,
    DefaultChanged,
    PropertyChanged
};

std::string deviceEventTypeToString(DeviceEventType type);

struct AudioDevice {
    std::string id;
    std::string name;
    DeviceState state = DeviceState::Unknown;
    bool isActive = false;
    bool isDefault = false;

    [[nodiscard]] std::string stateString() const {
        return deviceStateToString(state);
    }
};

struct DeviceEvent {
    DeviceEventType type;
    std::string deviceId;
    DeviceState newState = DeviceState::Unknown;
    std::optional<AudioDevice> device;
    std::string details;

    [[nodiscard]] std::string typeString() const {
        return deviceEventTypeToString(type);
    }
};

using DeviceEventCallback = std::function<void(const DeviceEvent&)>;

class DeviceManager {
public:
    DeviceManager();
    ~DeviceManager();

    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;
    DeviceManager(DeviceManager&&) noexcept;
    DeviceManager& operator=(DeviceManager&&) noexcept;

    // Enumerate render endpoints. If activeOnly is true, only returns active devices.
    [[nodiscard]] std::vector<AudioDevice> enumerateDevices(bool activeOnly = false);

    // Get the current default audio render endpoint
    [[nodiscard]] std::optional<AudioDevice> getDefaultDevice();

    // Get a specific device by its opaque endpoint ID
    [[nodiscard]] std::optional<AudioDevice> getDeviceById(const std::string& id);

    // Get a device by index from the latest enumeration
    [[nodiscard]] std::optional<AudioDevice> getDeviceByIndex(size_t index, bool activeOnly = false);

    // Start listening for real-time Windows Core Audio endpoint notifications
    bool startMonitoring(DeviceEventCallback callback);

    // Stop listening for notifications
    void stopMonitoring();

    // Check if monitoring is currently active
    [[nodiscard]] bool isMonitoring() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace syncwave
