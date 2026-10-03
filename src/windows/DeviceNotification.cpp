#include "DeviceNotification.h"
#include "ComHelper.h"

namespace syncwave {

DeviceNotificationClient::DeviceNotificationClient(INotificationListener* listener)
    : refCount_(1), listener_(listener) {}

DeviceNotificationClient::~DeviceNotificationClient() = default;

void DeviceNotificationClient::setListener(INotificationListener* listener) {
    listener_.store(listener, std::memory_order_release);
}

HRESULT STDMETHODCALLTYPE DeviceNotificationClient::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) {
        return E_POINTER;
    }

    if (riid == __uuidof(IUnknown)) {
        *ppvObject = static_cast<IUnknown*>(this);
    } else if (riid == __uuidof(IMMNotificationClient)) {
        *ppvObject = static_cast<IMMNotificationClient*>(this);
    } else {
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE DeviceNotificationClient::AddRef() {
    return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE DeviceNotificationClient::Release() {
    ULONG count = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (count == 0) {
        delete this;
    }
    return count;
}

HRESULT STDMETHODCALLTYPE DeviceNotificationClient::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) {
    auto* listener = listener_.load(std::memory_order_acquire);
    if (listener && pwstrDeviceId) {
        std::string id = wideToUtf8(pwstrDeviceId);
        listener->onDeviceStateChanged(id, dwNewState);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DeviceNotificationClient::OnDeviceAdded(LPCWSTR pwstrDeviceId) {
    auto* listener = listener_.load(std::memory_order_acquire);
    if (listener && pwstrDeviceId) {
        std::string id = wideToUtf8(pwstrDeviceId);
        listener->onDeviceAdded(id);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DeviceNotificationClient::OnDeviceRemoved(LPCWSTR pwstrDeviceId) {
    auto* listener = listener_.load(std::memory_order_acquire);
    if (listener && pwstrDeviceId) {
        std::string id = wideToUtf8(pwstrDeviceId);
        listener->onDeviceRemoved(id);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DeviceNotificationClient::OnDefaultDeviceChanged(
    EDataFlow flow,
    ERole role,
    LPCWSTR pwstrDefaultDeviceId)
{
    auto* listener = listener_.load(std::memory_order_acquire);
    if (listener) {
        std::string id = pwstrDefaultDeviceId ? wideToUtf8(pwstrDefaultDeviceId) : "";
        listener->onDefaultDeviceChanged(flow, role, id);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DeviceNotificationClient::OnPropertyValueChanged(
    LPCWSTR pwstrDeviceId,
    const PROPERTYKEY key)
{
    auto* listener = listener_.load(std::memory_order_acquire);
    if (listener && pwstrDeviceId) {
        std::string id = wideToUtf8(pwstrDeviceId);
        listener->onPropertyValueChanged(id, key);
    }
    return S_OK;
}

} // namespace syncwave
