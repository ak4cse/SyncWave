#pragma once

#include "ComHelper.h"
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <algorithm>
#include <string>

namespace syncwave {

// Controls the true Windows hardware/endpoint master volume (0.0 .. 1.0)
// and mute state for any specific IMMDevice by its endpoint ID,
// completely independent of system/app mixer volume.
class EndpointVolumeController {
public:
    static bool setMasterVolume(const std::string& deviceId, float scalar01) {
        scalar01 = std::max(0.0f, std::min(1.0f, scalar01));
        return withEndpointVolume(deviceId, [&](IAudioEndpointVolume* vol) {
            return SUCCEEDED(vol->SetMasterVolumeLevelScalar(scalar01, nullptr));
        });
    }

    static bool getMasterVolume(const std::string& deviceId, float& outScalar01) {
        return withEndpointVolume(deviceId, [&](IAudioEndpointVolume* vol) {
            return SUCCEEDED(vol->GetMasterVolumeLevelScalar(&outScalar01));
        });
    }

    static bool setMute(const std::string& deviceId, bool muted) {
        return withEndpointVolume(deviceId, [&](IAudioEndpointVolume* vol) {
            return SUCCEEDED(vol->SetMute(muted ? TRUE : FALSE, nullptr));
        });
    }

    static bool getMute(const std::string& deviceId, bool& outMuted) {
        return withEndpointVolume(deviceId, [&](IAudioEndpointVolume* vol) {
            BOOL m = FALSE;
            if (SUCCEEDED(vol->GetMute(&m))) {
                outMuted = (m != FALSE);
                return true;
            }
            return false;
        });
    }

private:
    template <typename Fn>
    static bool withEndpointVolume(const std::string& deviceId, Fn&& fn) {
        if (deviceId.empty()) return false;
        std::wstring wideId = utf8ToWide(deviceId);
        if (wideId.empty()) return false;

        HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool shouldUninit = SUCCEEDED(hrInit);

        bool ok = false;
        IMMDeviceEnumerator* enumerator = nullptr;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&enumerator)
        );

        if (SUCCEEDED(hr) && enumerator) {
            IMMDevice* device = nullptr;
            if (SUCCEEDED(enumerator->GetDevice(wideId.c_str(), &device)) && device) {
                IAudioEndpointVolume* endpointVol = nullptr;
                if (SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                               reinterpret_cast<void**>(&endpointVol))) && endpointVol) {
                    ok = fn(endpointVol);
                    endpointVol->Release();
                }
                device->Release();
            }
            enumerator->Release();
        }

        if (shouldUninit) {
            CoUninitialize();
        }
        return ok;
    }
};

} // namespace syncwave
