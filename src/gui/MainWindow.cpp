#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "MainWindow.h"
#include "StudioUiHtml.h"
#include "../windows/ComHelper.h"
#include <dwmapi.h>
#include <shlobj.h>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <cmath>
#include <algorithm>

namespace syncwave {

namespace {

// Minimal JSON string escaping
std::string escapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) >= 0x20) {
                    out += c;
                }
                break;
        }
    }
    return out;
}

// Extract simple JSON string value for a given key
std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\":\"";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return {};
    pos += pat.size();
    std::string val;
    for (size_t i = pos; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            val += json[++i];
        } else if (json[i] == '\"') {
            break;
        } else {
            val += json[i];
        }
    }
    return val;
}

// Extract simple JSON number value for a given key
double extractJsonNumber(const std::string& json, const std::string& key, double fallback = 0.0) {
    std::string pat = "\"" + key + "\":";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return fallback;
    pos += pat.size();
    try {
        return std::stod(json.substr(pos));
    } catch (...) {
        return fallback;
    }
}

bool extractJsonBool(const std::string& json, const std::string& key, bool fallback = false) {
    std::string pat = "\"" + key + "\":";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return fallback;
    pos += pat.size();
    if (json.compare(pos, 4, "true") == 0) return true;
    if (json.compare(pos, 5, "false") == 0) return false;
    return fallback;
}

std::vector<std::string> extractJsonStringArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    std::string pat = "\"" + key + "\":[";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return result;
    pos += pat.size();
    size_t end = json.find(']', pos);
    if (end == std::string::npos) return result;

    size_t i = pos;
    while (i < end) {
        size_t q1 = json.find('\"', i);
        if (q1 == std::string::npos || q1 >= end) break;
        q1++;
        std::string item;
        size_t j = q1;
        for (; j < end; ++j) {
            if (json[j] == '\\' && j + 1 < end) {
                item += json[++j];
            } else if (json[j] == '\"') {
                break;
            } else {
                item += json[j];
            }
        }
        result.push_back(item);
        i = j + 1;
    }
    return result;
}

std::vector<double> extractJsonNumberArray(const std::string& json, const std::string& key) {
    std::vector<double> result;
    std::string pat = "\"" + key + "\":[";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return result;
    pos += pat.size();
    size_t end = json.find(']', pos);
    if (end == std::string::npos) return result;

    std::stringstream ss(json.substr(pos, end - pos));
    std::string item;
    while (std::getline(ss, item, ',')) {
        try {
            result.push_back(std::stod(item));
        } catch (...) {}
    }
    return result;
}

// Minimal COM callback wrappers for WebView2
class EnvCompletedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
public:
    explicit EnvCompletedHandler(MainWindow* owner) : owner_(owner) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler) {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG c = --ref_;
        if (c == 0) delete this;
        return c;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Environment* env) override {
        if (owner_) owner_->onWebViewEnvironmentCreated(result, env);
        return S_OK;
    }
private:
    MainWindow* owner_;
    std::atomic<ULONG> ref_{1};
};

class ControllerCompletedHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
public:
    explicit ControllerCompletedHandler(MainWindow* owner) : owner_(owner) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler) {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG c = --ref_;
        if (c == 0) delete this;
        return c;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller* controller) override {
        if (owner_) owner_->onWebViewControllerCreated(result, controller);
        return S_OK;
    }
private:
    MainWindow* owner_;
    std::atomic<ULONG> ref_{1};
};

class WebMessageHandler : public ICoreWebView2WebMessageReceivedEventHandler {
public:
    explicit WebMessageHandler(MainWindow* owner) : owner_(owner) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2WebMessageReceivedEventHandler) {
            *ppv = static_cast<ICoreWebView2WebMessageReceivedEventHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG c = --ref_;
        if (c == 0) delete this;
        return c;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* /*sender*/, ICoreWebView2WebMessageReceivedEventArgs* args) override {
        if (!args || !owner_) return S_OK;
        LPWSTR msg = nullptr;
        if (SUCCEEDED(args->get_WebMessageAsJson(&msg)) && msg) {
            owner_->onWebMessageReceived(msg);
            CoTaskMemFree(msg);
        }
        return S_OK;
    }
private:
    MainWindow* owner_;
    std::atomic<ULONG> ref_{1};
};

constexpr UINT_PTR kTelemetryTimerId = 1001;

} // namespace

MainWindow::MainWindow(HINSTANCE hInstance, std::shared_ptr<ISyncWaveEngine> engine)
    : hInstance_(hInstance), engine_(std::move(engine)) {}

MainWindow::~MainWindow() {
    if (webView_ && webMsgToken_.value != 0) {
        webView_->remove_WebMessageReceived(webMsgToken_);
    }
    if (webView_) {
        webView_->Release();
        webView_ = nullptr;
    }
    if (webViewController_) {
        webViewController_->Close();
        webViewController_->Release();
        webViewController_ = nullptr;
    }
    if (webViewEnv_) {
        webViewEnv_->Release();
        webViewEnv_ = nullptr;
    }
    if (hWebView2Loader_) {
        FreeLibrary(hWebView2Loader_);
        hWebView2Loader_ = nullptr;
    }
}

bool MainWindow::create(int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"SyncWaveStudioWindowClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWindow::WndProc;
    wc.hInstance = hInstance_;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassW(&wc);

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rc = {0, 0, 960, 680};
    AdjustWindowRect(&rc, style, FALSE);

    hWnd_ = CreateWindowExW(
        0,
        CLASS_NAME,
        L"SyncWave",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr,
        nullptr,
        hInstance_,
        this
    );

    if (!hWnd_) return false;

    // Enable Windows 11 dark titlebar
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hWnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &darkMode, sizeof(darkMode));

    ShowWindow(hWnd_, nCmdShow);
    UpdateWindow(hWnd_);
    return true;
}

int MainWindow::runMessageLoop() {
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = nullptr;
    if (message == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<MainWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->hWnd_ = hWnd;
    } else {
        pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->handleMessage(hWnd, message, wParam, lParam);
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

LRESULT MainWindow::handleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            initWebView();
            SetTimer(hWnd, kTelemetryTimerId, 250, nullptr);
            return 0;

        case WM_SIZE:
            resizeWebView();
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = 760;
            mmi->ptMinTrackSize.y = 540;
            return 0;
        }

        case WM_TIMER:
            if (wParam == kTelemetryTimerId) {
                pushTelemetryToUi();
            }
            return 0;

        case WM_CLOSE:
            KillTimer(hWnd, kTelemetryTimerId);
            if (engine_) {
                engine_->stop();
            }
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

void MainWindow::initWebView() {
    hWebView2Loader_ = LoadLibraryW(L"WebView2Loader.dll");
    if (!hWebView2Loader_) {
        MessageBoxW(hWnd_, L"WebView2Loader.dll not found in PATH.", L"SyncWave UI Error", MB_ICONERROR);
        return;
    }

    using CreateEnvFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                                 ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto pCreateEnv = reinterpret_cast<CreateEnvFn>(
        reinterpret_cast<void*>(GetProcAddress(hWebView2Loader_, "CreateCoreWebView2EnvironmentWithOptions")));
    if (!pCreateEnv) {
        MessageBoxW(hWnd_, L"Failed to resolve WebView2 entry point.", L"SyncWave UI Error", MB_ICONERROR);
        return;
    }

    wchar_t localAppData[MAX_PATH] = {};
    std::wstring userDataDir;
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
        userDataDir = std::wstring(localAppData) + L"\\SyncWave\\WebView2";
    }

    auto* handler = new EnvCompletedHandler(this);
    pCreateEnv(nullptr, userDataDir.empty() ? nullptr : userDataDir.c_str(), nullptr, handler);
    handler->Release();
}

void MainWindow::onWebViewEnvironmentCreated(HRESULT result, ICoreWebView2Environment* env) {
    if (FAILED(result) || !env) {
        MessageBoxW(hWnd_, L"Failed to initialize Microsoft Edge WebView2 Runtime.", L"SyncWave UI Error", MB_ICONERROR);
        return;
    }
    webViewEnv_ = env;
    webViewEnv_->AddRef();

    auto* handler = new ControllerCompletedHandler(this);
    webViewEnv_->CreateCoreWebView2Controller(hWnd_, handler);
    handler->Release();
}

void MainWindow::onWebViewControllerCreated(HRESULT result, ICoreWebView2Controller* controller) {
    if (FAILED(result) || !controller) return;

    webViewController_ = controller;
    webViewController_->AddRef();
    webViewController_->get_CoreWebView2(&webView_);

    if (webView_) {
        ICoreWebView2Settings* settings = nullptr;
        if (SUCCEEDED(webView_->get_Settings(&settings)) && settings) {
            settings->put_AreDefaultContextMenusEnabled(FALSE);
            settings->put_AreDevToolsEnabled(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_IsZoomControlEnabled(FALSE);
            settings->Release();
        }

        auto* msgHandler = new WebMessageHandler(this);
        webView_->add_WebMessageReceived(msgHandler, &webMsgToken_);
        msgHandler->Release();

        std::wstring htmlW = utf8ToWide(kStudioUiHtml);
        webView_->NavigateToString(htmlW.c_str());
    }

    resizeWebView();
}

void MainWindow::resizeWebView() {
    if (!webViewController_ || !hWnd_) return;
    RECT bounds = {};
    GetClientRect(hWnd_, &bounds);
    webViewController_->put_Bounds(bounds);
}

void MainWindow::sendJsonToUi(const std::string& json) {
    if (!webView_) return;
    std::wstring wJson = utf8ToWide(json);
    webView_->PostWebMessageAsJson(wJson.c_str());
}

void MainWindow::onWebMessageReceived(const std::wstring& messageJson) {
    std::string json = wideToUtf8(messageJson);
    std::string cmd = extractJsonString(json, "cmd");

    if (cmd == "init" || cmd == "refreshDevices") {
        pushDeviceListToUi();
        pushTelemetryToUi();
    } else if (cmd == "setSyncMode") {
        std::string mode = extractJsonString(json, "mode");
        if (!mode.empty()) {
            engine_->setSyncMode(mode);
        }
    } else if (cmd == "startSync") {
        std::string mode = extractJsonString(json, "syncMode");
        if (!mode.empty()) {
            engine_->setSyncMode(mode);
        }
        auto outputs = extractJsonStringArray(json, "outputs");
        bool ok = engine_->startAutoSystemCapture(outputs);
        if (!ok) {
            std::string err = engine_->getLastError();
            sendJsonToUi("{\"type\":\"error\",\"message\":\"" + escapeJson(err) + "\"}");
        } else {
            sendJsonToUi("{\"type\":\"error\",\"message\":\"\"}");
        }
        pushTelemetryToUi();
    } else if (cmd == "stopSync") {
        engine_->stop();
        sendJsonToUi("{\"type\":\"error\",\"message\":\"\"}");
        pushTelemetryToUi();
    } else if (cmd == "setDeviceVolume") {
        size_t idx = static_cast<size_t>(std::max(0.0, extractJsonNumber(json, "index")));
        float vol = static_cast<float>(extractJsonNumber(json, "volume", 1.0));
        engine_->setDeviceVolume(idx, vol);
    } else if (cmd == "setDeviceMuted") {
        size_t idx = static_cast<size_t>(std::max(0.0, extractJsonNumber(json, "index")));
        bool muted = extractJsonBool(json, "muted", false);
        engine_->setDeviceMuted(idx, muted);
    } else if (cmd == "setEndpointVolume") {
        std::string id = extractJsonString(json, "id");
        float vol = static_cast<float>(extractJsonNumber(json, "volume", 0.5));
        engine_->setEndpointMasterVolume(id, vol);
    } else if (cmd == "setEndpointMuted") {
        std::string id = extractJsonString(json, "id");
        bool muted = extractJsonBool(json, "muted", false);
        engine_->setEndpointMasterMuted(id, muted);
    } else if (cmd == "setDelays") {
        auto delays = extractJsonNumberArray(json, "delays");
        engine_->setManualDelays(delays);
    }
}

void MainWindow::pushDeviceListToUi() {
    auto devices = engine_->enumerateOutputDevices(true);
    std::ostringstream oss;
    oss << "{\"type\":\"devices\",\"devices\":[";
    for (size_t i = 0; i < devices.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& d = devices[i];
        bool isVirtual = (d.name.find("Steam Streaming") != std::string::npos);
        bool isBluetooth = (d.name.find("Headphones") != std::string::npos ||
                            d.name.find("Buds") != std::string::npos ||
                            d.name.find("AirPods") != std::string::npos ||
                            d.name.find("Bluetooth") != std::string::npos);
        oss << "{"
            << "\"id\":\"" << escapeJson(d.id) << "\","
            << "\"name\":\"" << escapeJson(d.name) << "\","
            << "\"isDefault\":" << (d.isDefault ? "true" : "false") << ","
            << "\"isVirtual\":" << (isVirtual ? "true" : "false") << ","
            << "\"isBluetooth\":" << (isBluetooth ? "true" : "false")
            << "}";
    }
    oss << "]}";
    sendJsonToUi(oss.str());
}

void MainWindow::pushTelemetryToUi() {
    if (!webView_) return;
    auto diag = engine_->getDiagnostics();

    double maxLatencyMs = 0.0;
    for (const auto& ep : diag.endpoints) {
        maxLatencyMs = std::max(maxLatencyMs, ep.effectiveLatencyMs);
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2)
        << "{\"type\":\"telemetry\","
        << "\"isRunning\":" << (diag.isEngineRunning ? "true" : "false") << ","
        << "\"activeSource\":\"" << escapeJson(diag.activeSource) << "\","
        << "\"sampleRate\":" << diag.masterSampleRate << ","
        << "\"cpu\":" << diag.system.processCpuPercent << ","
        << "\"memoryMb\":" << diag.system.memoryUsageMb << ","
        << "\"maxLatencyMs\":" << maxLatencyMs << ","
        << "\"endpoints\":[";

    for (size_t i = 0; i < diag.endpoints.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& ep = diag.endpoints[i];
        oss << "{"
            << "\"id\":\"" << escapeJson(ep.deviceId) << "\","
            << "\"name\":\"" << escapeJson(ep.friendlyName) << "\","
            << "\"syncState\":\"" << escapeJson(syncStateToString(ep.syncState)) << "\","
            << "\"driftState\":\"" << escapeJson(ep.driftState) << "\","
            << "\"driftPpm\":" << ep.filteredDriftPpm << ","
            << "\"phaseErrorMs\":" << ep.filteredPhaseErrorMs << ","
            << "\"delayMs\":" << ep.configuredDelayMs << ","
            << "\"effectiveLatencyMs\":" << ep.effectiveLatencyMs << ","
            << "\"underruns\":" << ep.wasapiUnderruns
            << "}";
    }
    oss << "]}";
    sendJsonToUi(oss.str());
}

} // namespace syncwave
