#include "MainWindow.h"
#include "../core/ISyncWaveEngine.h"
#include <windows.h>
#include <memory>
#include <iostream>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR /*pCmdLine*/, int nCmdShow) {
    // Enable High DPI Awareness dynamically if supported
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef BOOL(WINAPI* SetProcessDpiAwarenessContextProc)(HANDLE);
        auto pSetDpi = (SetProcessDpiAwarenessContextProc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetDpi) {
            pSetDpi((HANDLE)-4); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        }
    }

    // Initialize COM MTA for audio background threads
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"Failed to initialize COM MTA subsystem.", L"SyncWave Error", MB_ICONERROR);
        return 1;
    }

    try {
        auto engine = syncwave::createSyncWaveEngine();
        if (!engine) {
            MessageBoxW(nullptr, L"Failed to initialize SyncWave core engine.", L"SyncWave Error", MB_ICONERROR);
            CoUninitialize();
            return 1;
        }

        syncwave::MainWindow mainWindow(hInstance, std::move(engine));
        if (!mainWindow.create(nCmdShow)) {
            MessageBoxW(nullptr, L"Failed to create SyncWave main window.", L"SyncWave Error", MB_ICONERROR);
            CoUninitialize();
            return 1;
        }

        int exitCode = mainWindow.runMessageLoop();
        CoUninitialize();
        return exitCode;
    } catch (const std::exception& e) {
        std::string err = std::string("Fatal exception: ") + e.what();
        std::wstring errW(err.begin(), err.end());
        MessageBoxW(nullptr, errW.c_str(), L"SyncWave Fatal Error", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
}
