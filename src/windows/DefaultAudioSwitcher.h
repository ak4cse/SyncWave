#pragma once

#include "ComHelper.h"
#include <mmdeviceapi.h>
#include <unknwn.h>
#include <string>

namespace syncwave {

// Undocumented Windows Core Audio PolicyConfig interface used to programmatically
// switch the default Windows playback endpoint (eConsole / eMultimedia / eCommunications).
interface DECLSPEC_UUID("f8679f50-850a-41cf-9c72-430f290290c8")
IPolicyConfig : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR wszDeviceId, ERole eRole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

class DefaultAudioSwitcher {
public:
    static bool setDefaultPlaybackDevice(const std::string& deviceId) {
        if (deviceId.empty()) return false;

        std::wstring wideId = utf8ToWide(deviceId);
        if (wideId.empty()) return false;

        // Ensure COM is initialized on the calling thread
        HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool shouldUninit = SUCCEEDED(hrInit);

        // CLSID_CPolicyConfigClient = {870af99c-171d-4f9e-af0d-e63df40c2bc9}
        static const CLSID CLSID_PolicyConfigClient = {
            0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}
        };
        static const IID IID_IPolicyConfigLocal = {
            0xf8679f50, 0x850a, 0x41cf, {0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8}
        };

        IPolicyConfig* pPolicyConfig = nullptr;
        HRESULT hr = CoCreateInstance(
            CLSID_PolicyConfigClient,
            nullptr,
            CLSCTX_ALL,
            IID_IPolicyConfigLocal,
            reinterpret_cast<void**>(&pPolicyConfig)
        );

        bool ok = false;
        if (SUCCEEDED(hr) && pPolicyConfig) {
            HRESULT hrConsole = pPolicyConfig->SetDefaultEndpoint(wideId.c_str(), eConsole);
            HRESULT hrMedia = pPolicyConfig->SetDefaultEndpoint(wideId.c_str(), eMultimedia);
            HRESULT hrComm = pPolicyConfig->SetDefaultEndpoint(wideId.c_str(), eCommunications);
            ok = SUCCEEDED(hrConsole) || SUCCEEDED(hrMedia) || SUCCEEDED(hrComm);
            pPolicyConfig->Release();
        }

        if (shouldUninit) {
            CoUninitialize();
        }
        return ok;
    }
};

} // namespace syncwave
