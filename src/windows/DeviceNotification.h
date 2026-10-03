#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <atomic>
#include <string>
#include <memory>

namespace syncwave {

class INotificationListener {
public:
    virtual ~INotificationListener() = default;
    virtual void onDeviceAdded(const std::string& deviceId) = 0;
    virtual void onDeviceRemoved(const std::string& deviceId) = 0;
    virtual void onDeviceStateChanged(const std::string& deviceId, DWORD newState) = 0;
    virtual void onDefaultDeviceChanged(EDataFlow flow, ERole role, const std::string& defaultDeviceId) = 0;
    virtual void onPropertyValueChanged(const std::string& deviceId, const PROPERTYKEY& key) = 0;
};

class DeviceNotificationClient : public IMMNotificationClient {
public:
    explicit DeviceNotificationClient(INotificationListener* listener = nullptr);
    virtual ~DeviceNotificationClient();

    // Set or clear the listener (thread-safe)
    void setListener(INotificationListener* listener);

    // IUnknown methods
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IMMNotificationClient methods
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override;
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) override;
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) override;
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override;
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override;

private:
    std::atomic<ULONG> refCount_{1};
    std::atomic<INotificationListener*> listener_{nullptr};
};

} // namespace syncwave
