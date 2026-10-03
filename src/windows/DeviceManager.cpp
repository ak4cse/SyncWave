#include "DeviceManager.h"
#include "ComHelper.h"

#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <stdexcept>
#include <iostream>

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

struct DeviceManager::Impl {
    ComInitializer comInit;
    ComPtr<IMMDeviceEnumerator> pEnumerator;

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
    if (!impl_ || !impl_->pEnumerator || id.empty()) {
        return std::nullopt;
    }

    std::wstring wid = utf8ToWide(id);
    ComPtr<IMMDevice> pDevice;
    HRESULT hr = impl_->pEnumerator->GetDevice(wid.c_str(), &pDevice);
    if (FAILED(hr) || !pDevice) {
        return std::nullopt;
    }

    std::string defaultId = impl_->getDefaultDeviceId();
    return buildAudioDeviceFromEndpoint(pDevice.Get(), defaultId);
}

std::optional<AudioDevice> DeviceManager::getDeviceByIndex(size_t index, bool activeOnly) {
    auto list = enumerateDevices(activeOnly);
    if (index < list.size()) {
        return list[index];
    }
    return std::nullopt;
}

} // namespace syncwave
