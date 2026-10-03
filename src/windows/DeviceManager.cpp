#include "DeviceManager.h"
#include "DeviceNotification.h"
#include "ComHelper.h"

#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <stdexcept>
#include <iostream>
#include <mutex>
#include <atomic>

using Microsoft::WRL::ComPtr;

namespace syncwave {

std::string deviceStateToString(DeviceState state) {
    switch (state) {
        case DeviceState::Active:     return "Active";
        case DeviceState::Disabled:   return "Disabled";
        case DeviceState::NotPresent: return "Not Present";
        case DeviceState::Unplugged:  return "Unplugged";
        case DeviceState::Unknown:
        default:                      return "Unknown";
    }
}

std::string deviceEventTypeToString(DeviceEventType type) {
    switch (type) {
        case DeviceEventType::Added:          return "Device Added";
        case DeviceEventType::Removed:        return "Device Removed";
        case DeviceEventType::StateChanged:   return "Device State Changed";
        case DeviceEventType::DefaultChanged: return "Default Device Changed";
        case DeviceEventType::PropertyChanged:return "Device Property Changed";
        default:                              return "Unknown Event";
    }
}

static DeviceState mapWindowsDeviceState(DWORD dwState) {
    if (dwState & DEVICE_STATE_ACTIVE) {
        return DeviceState::Active;
    }
    if (dwState & DEVICE_STATE_DISABLED) {
        return DeviceState::Disabled;
    }
    if (dwState & DEVICE_STATE_NOTPRESENT) {
        return DeviceState::NotPresent;
    }
    if (dwState & DEVICE_STATE_UNPLUGGED) {
        return DeviceState::Unplugged;
    }
    return DeviceState::Unknown;
}

static std::optional<std::string> getDevicePropertyString(IPropertyStore* pProps, const PROPERTYKEY& key) {
    if (!pProps) return std::nullopt;

    PROPVARIANT var;
    PropVariantInit(&var);
    HRESULT hr = pProps->GetValue(key, &var);
    if (FAILED(hr)) {
        return std::nullopt;
    }

    std::string result;
    if (var.vt == VT_LPWSTR && var.pwszVal) {
        result = wideToUtf8(var.pwszVal);
    }
    PropVariantClear(&var);

    if (result.empty()) return std::nullopt;
    return result;
}

static std::optional<AudioDevice> buildAudioDeviceFromEndpoint(
    IMMDevice* pDevice,
    const std::string& defaultDeviceId)
{
    if (!pDevice) return std::nullopt;

    LPWSTR pstrId = nullptr;
    HRESULT hr = pDevice->GetId(&pstrId);
    if (FAILED(hr) || !pstrId) {
        return std::nullopt;
    }

    std::string deviceId = wideToUtf8(pstrId);
    CoTaskMemFree(pstrId);

    DWORD dwState = 0;
    hr = pDevice->GetState(&dwState);
    DeviceState state = SUCCEEDED(hr) ? mapWindowsDeviceState(dwState) : DeviceState::Unknown;

    ComPtr<IPropertyStore> pProps;
    hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
    std::string friendlyName;
    if (SUCCEEDED(hr) && pProps) {
        auto propStr = getDevicePropertyString(pProps.Get(), PKEY_Device_FriendlyName);
        if (propStr) {
            friendlyName = *propStr;
        }
    }

    if (friendlyName.empty()) {
        friendlyName = "Unknown Audio Device";
    }

    AudioDevice device;
    device.id = deviceId;
    device.name = friendlyName;
    device.state = state;
    device.isActive = (state == DeviceState::Active);
    device.isDefault = (!defaultDeviceId.empty() && deviceId == defaultDeviceId);

    return device;
}

struct DeviceManager::Impl : public INotificationListener {
    ComInitializer comInit;
    ComPtr<IMMDeviceEnumerator> pEnumerator;
    ComPtr<DeviceNotificationClient> pNotificationClient;

    std::mutex callbackMutex;
    DeviceEventCallback eventCallback;
    std::atomic<bool> isMonitoring_{false};

    Impl() {
        if (!comInit.succeeded()) {
            std::cerr << "Warning: CoInitializeEx returned " << formatHResult(comInit.result()) << "\n";
        }

        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(&pEnumerator)
        );

        if (FAILED(hr)) {
            std::string err = "Failed to create MMDeviceEnumerator instance: " + formatHResult(hr);
            throw std::runtime_error(err);
        }
    }

    ~Impl() override {
        stopMonitoring();
    }

    std::string getDefaultDeviceId() const {
        if (!pEnumerator) return {};

        ComPtr<IMMDevice> pDefaultDevice;
        HRESULT hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDefaultDevice);
        if (FAILED(hr) || !pDefaultDevice) {
            return {};
        }

        LPWSTR pstrId = nullptr;
        hr = pDefaultDevice->GetId(&pstrId);
        if (FAILED(hr) || !pstrId) {
            return {};
        }

        std::string defaultId = wideToUtf8(pstrId);
        CoTaskMemFree(pstrId);
        return defaultId;
    }

    std::optional<AudioDevice> getDeviceById(const std::string& id) {
        if (!pEnumerator || id.empty()) {
            return std::nullopt;
        }

        std::wstring wid = utf8ToWide(id);
        ComPtr<IMMDevice> pDevice;
        HRESULT hr = pEnumerator->GetDevice(wid.c_str(), &pDevice);
        if (FAILED(hr) || !pDevice) {
            return std::nullopt;
        }

        std::string defaultId = getDefaultDeviceId();
        return buildAudioDeviceFromEndpoint(pDevice.Get(), defaultId);
    }

    bool startMonitoring(DeviceEventCallback callback) {
        if (isMonitoring_.load(std::memory_order_acquire)) {
            return true;
        }

        {
            std::lock_guard<std::mutex> lock(callbackMutex);
            eventCallback = std::move(callback);
        }

        auto* client = new DeviceNotificationClient(this);
        pNotificationClient.Attach(client);

        HRESULT hr = pEnumerator->RegisterEndpointNotificationCallback(pNotificationClient.Get());
        if (FAILED(hr)) {
            std::cerr << "Failed to register endpoint notification callback: " << formatHResult(hr) << "\n";
            pNotificationClient.Reset();
            return false;
        }

        isMonitoring_.store(true, std::memory_order_release);
        return true;
    }

    void stopMonitoring() {
        if (!isMonitoring_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }

        if (pNotificationClient) {
            pNotificationClient->setListener(nullptr);
            if (pEnumerator) {
                pEnumerator->UnregisterEndpointNotificationCallback(pNotificationClient.Get());
            }
            pNotificationClient.Reset();
        }

        {
            std::lock_guard<std::mutex> lock(callbackMutex);
            eventCallback = nullptr;
        }
    }

    void dispatchEvent(const DeviceEvent& event) {
        DeviceEventCallback cb;
        {
            std::lock_guard<std::mutex> lock(callbackMutex);
            cb = eventCallback;
        }
        if (cb) {
            cb(event);
        }
    }

    // INotificationListener implementation
    void onDeviceAdded(const std::string& deviceId) override {
        auto dev = getDeviceById(deviceId);
        DeviceEvent ev;
        ev.type = DeviceEventType::Added;
        ev.deviceId = deviceId;
        ev.newState = dev ? dev->state : DeviceState::Active;
        ev.device = dev;
        ev.details = dev ? dev->name : "";
        dispatchEvent(ev);
    }

    void onDeviceRemoved(const std::string& deviceId) override {
        DeviceEvent ev;
        ev.type = DeviceEventType::Removed;
        ev.deviceId = deviceId;
        ev.newState = DeviceState::NotPresent;
        ev.device = std::nullopt;
        ev.details = "";
        dispatchEvent(ev);
    }

    void onDeviceStateChanged(const std::string& deviceId, DWORD newState) override {
        DeviceState mapped = mapWindowsDeviceState(newState);
        auto dev = getDeviceById(deviceId);
        DeviceEvent ev;
        ev.type = DeviceEventType::StateChanged;
        ev.deviceId = deviceId;
        ev.newState = mapped;
        ev.device = dev;
        ev.details = "State: " + deviceStateToString(mapped);
        dispatchEvent(ev);
    }

    void onDefaultDeviceChanged(EDataFlow flow, ERole role, const std::string& defaultDeviceId) override {
        if (flow != eRender) {
            return;
        }
        if (role != eConsole && role != eMultimedia) {
            return;
        }

        std::string roleName = (role == eConsole) ? "Console" : "Multimedia";
        auto dev = getDeviceById(defaultDeviceId);
        DeviceEvent ev;
        ev.type = DeviceEventType::DefaultChanged;
        ev.deviceId = defaultDeviceId;
        ev.newState = dev ? dev->state : DeviceState::Active;
        ev.device = dev;
        ev.details = "Role: " + roleName;
        dispatchEvent(ev);
    }

    void onPropertyValueChanged(const std::string& deviceId, const PROPERTYKEY& key) override {
        auto dev = getDeviceById(deviceId);
        std::string propName = "Property";
        if (key.pid == PKEY_Device_FriendlyName.pid &&
            InlineIsEqualGUID(key.fmtid, PKEY_Device_FriendlyName.fmtid)) {
            propName = "FriendlyName";
        }
        DeviceEvent ev;
        ev.type = DeviceEventType::PropertyChanged;
        ev.deviceId = deviceId;
        ev.newState = dev ? dev->state : DeviceState::Unknown;
        ev.device = dev;
        ev.details = propName;
        dispatchEvent(ev);
    }
};

DeviceManager::DeviceManager()
    : impl_(std::make_unique<Impl>()) {}

DeviceManager::~DeviceManager() = default;
DeviceManager::DeviceManager(DeviceManager&&) noexcept = default;
DeviceManager& DeviceManager::operator=(DeviceManager&&) noexcept = default;

std::vector<AudioDevice> DeviceManager::enumerateDevices(bool activeOnly) {
    std::vector<AudioDevice> devices;
    if (!impl_ || !impl_->pEnumerator) {
        return devices;
    }

    std::string defaultId = impl_->getDefaultDeviceId();

    DWORD stateMask = activeOnly ? DEVICE_STATE_ACTIVE : DEVICE_STATEMASK_ALL;
    ComPtr<IMMDeviceCollection> pCollection;
    HRESULT hr = impl_->pEnumerator->EnumAudioEndpoints(eRender, stateMask, &pCollection);
    if (FAILED(hr) || !pCollection) {
        std::cerr << "Failed to enumerate audio endpoints: " << formatHResult(hr) << "\n";
        return devices;
    }

    UINT count = 0;
    hr = pCollection->GetCount(&count);
    if (FAILED(hr)) {
        std::cerr << "Failed to get endpoint collection count: " << formatHResult(hr) << "\n";
        return devices;
    }

    devices.reserve(count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> pEndpoint;
        hr = pCollection->Item(i, &pEndpoint);
        if (FAILED(hr) || !pEndpoint) {
            continue;
        }

        auto device = buildAudioDeviceFromEndpoint(pEndpoint.Get(), defaultId);
        if (device) {
            devices.push_back(*device);
        }
    }

    return devices;
}

std::optional<AudioDevice> DeviceManager::getDefaultDevice() {
    if (!impl_ || !impl_->pEnumerator) {
        return std::nullopt;
    }

    ComPtr<IMMDevice> pDefaultDevice;
    HRESULT hr = impl_->pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDefaultDevice);
    if (FAILED(hr) || !pDefaultDevice) {
        return std::nullopt;
    }

    std::string defaultId = impl_->getDefaultDeviceId();
    return buildAudioDeviceFromEndpoint(pDefaultDevice.Get(), defaultId);
}

std::optional<AudioDevice> DeviceManager::getDeviceById(const std::string& id) {
    if (!impl_) {
        return std::nullopt;
    }
    return impl_->getDeviceById(id);
}

std::optional<AudioDevice> DeviceManager::getDeviceByIndex(size_t index, bool activeOnly) {
    auto list = enumerateDevices(activeOnly);
    if (index < list.size()) {
        return list[index];
    }
    return std::nullopt;
}

bool DeviceManager::startMonitoring(DeviceEventCallback callback) {
    if (!impl_) return false;
    return impl_->startMonitoring(std::move(callback));
}

void DeviceManager::stopMonitoring() {
    if (impl_) {
        impl_->stopMonitoring();
    }
}

bool DeviceManager::isMonitoring() const {
    return impl_ && impl_->isMonitoring_.load(std::memory_order_acquire);
}

} // namespace syncwave
