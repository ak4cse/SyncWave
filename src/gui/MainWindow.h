#pragma once

#include "../core/ISyncWaveEngine.h"
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <memory>
#include <chrono>

namespace syncwave {

class MainWindow {
public:
    MainWindow(HINSTANCE hInstance, std::shared_ptr<ISyncWaveEngine> engine);
    ~MainWindow();

    bool create(int nCmdShow);
    int runMessageLoop();

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    void setupControls(HWND hWnd);
    void updateDpiFont(HWND hWnd);
    void populateDeviceList();
    void onSyncModeChanged();
    void onOpenFileClicked();
    void onPlayClicked();
    void onPauseClicked();
    void onStopClicked();
    void onSliderSeek(int position);
    void onTimerTick();
    void updateTelemetryDisplay(const FullDiagnosticsSnapshot& diag);

    HINSTANCE hInstance_{nullptr};
    HWND hWnd_{nullptr};
    HFONT hFont_{nullptr};
    HFONT hFontBold_{nullptr};
    HFONT hFontMono_{nullptr};

    // Engine
    std::shared_ptr<ISyncWaveEngine> engine_;

    // Device selection state
    std::vector<AudioDevice> availableDevices_;
    std::vector<std::string> selectedDeviceIds_;

    // Media state
    std::string currentMediaPath_;
    bool isUserSeeking_{false};
    bool isPaused_{false};
    double currentDurationMs_{0.0};

    // Controls
    HWND hGrpDevices_{nullptr};
    HWND hListDevices_{nullptr}; // ListView with checkboxes
    HWND hBtnRefreshDevices_{nullptr};

    HWND hGrpSync_{nullptr};
    HWND hLblSyncMode_{nullptr};
    HWND hCmbSyncMode_{nullptr};

    HWND hGrpMedia_{nullptr};
    HWND hBtnOpenFile_{nullptr};
    HWND hLblMediaPath_{nullptr};
    HWND hBtnPlay_{nullptr};
    HWND hBtnPause_{nullptr};
    HWND hBtnStop_{nullptr};
    HWND hSliderProgress_{nullptr};
    HWND hLblTimeProgress_{nullptr};

    HWND hGrpLiveSync_{nullptr};
    HWND hListLiveSync_{nullptr}; // ListView showing live endpoint metrics

    HWND hGrpMediaTel_{nullptr};
    HWND hLblMediaTel_{nullptr};

    HWND hGrpSystem_{nullptr};
    HWND hLblSystemTel_{nullptr};

    HWND hStatusBar_{nullptr};

    UINT_PTR timerId_{1001};
};

} // namespace syncwave
