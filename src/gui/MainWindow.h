#pragma once

#include "../core/ISyncWaveEngine.h"
#include <windows.h>
#include <WebView2.h>
#include <string>
#include <vector>
#include <memory>

namespace syncwave {

class MainWindow {
public:
    MainWindow(HINSTANCE hInstance, std::shared_ptr<ISyncWaveEngine> engine);
    ~MainWindow();

    bool create(int nCmdShow);
    int runMessageLoop();

    // Called by WebView2 COM handlers
    void onWebViewEnvironmentCreated(HRESULT result, ICoreWebView2Environment* env);
    void onWebViewControllerCreated(HRESULT result, ICoreWebView2Controller* controller);
    void onWebMessageReceived(const std::wstring& messageJson);

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    void initWebView();
    void resizeWebView();
    void sendJsonToUi(const std::string& json);
    void pushDeviceListToUi();
    void pushTelemetryToUi();

    HINSTANCE hInstance_{nullptr};
    HWND hWnd_{nullptr};
    std::shared_ptr<ISyncWaveEngine> engine_;

    HMODULE hWebView2Loader_{nullptr};
    ICoreWebView2Environment* webViewEnv_{nullptr};
    ICoreWebView2Controller* webViewController_{nullptr};
    ICoreWebView2* webView_{nullptr};
    EventRegistrationToken webMsgToken_{};
};

} // namespace syncwave
