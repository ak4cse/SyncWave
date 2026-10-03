#pragma once

#include <windows.h>
#include <string>
#include <sstream>
#include <iomanip>

namespace syncwave {

class ComInitializer {
public:
    explicit ComInitializer(DWORD coinit = COINIT_MULTITHREADED) {
        hr_ = CoInitializeEx(nullptr, coinit);
    }

    ~ComInitializer() {
        if (SUCCEEDED(hr_)) {
            CoUninitialize();
        }
    }

    ComInitializer(const ComInitializer&) = delete;
    ComInitializer& operator=(const ComInitializer&) = delete;

    [[nodiscard]] bool succeeded() const { return SUCCEEDED(hr_); }
    [[nodiscard]] HRESULT result() const { return hr_; }

private:
    HRESULT hr_ = E_FAIL;
};

inline std::string formatHResult(HRESULT hr) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << static_cast<unsigned long>(hr);
    return oss.str();
}

inline std::string wideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return {};
    std::string strTo(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), strTo.data(), sizeNeeded, nullptr, nullptr);
    return strTo;
}

inline std::wstring utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (sizeNeeded <= 0) return {};
    std::wstring wstrTo(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), wstrTo.data(), sizeNeeded);
    return wstrTo;
}

} // namespace syncwave
