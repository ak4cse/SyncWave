#pragma once

#include "../core/ISyncWaveEngine.h"
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <memory>
#include <array>

namespace syncwave {

class MainWindow {
public:
    static constexpr int kMaxDeviceControls = 4;

    MainWindow(HINSTANCE hInstance, std::shared_ptr<ISyncWaveEngine> engine);
    ~MainWindow();

    bool create(int nCmdShow);
    int runMessageLoop();

private:
    struct DeviceControlRow {
        HWND hLblName{nullptr};
        HWND hChkMute{nullptr};
        HWND hSliderVol{nullptr};
        HWND hLblVol{nullptr};
        HWND hSliderDelay{nullptr};
        HWND hLblDelay{nullptr};
        int volumePercent{100};
        int delayMs{0};
        bool muted{false};
    };

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    void setupControls(HWND hWnd);
    void updateDpiFont(HWND hWnd);
    void populateDeviceList();
    void syncDeviceControlRows();
    std::vector<std::string> collectSelectedDeviceIds() const;

    void onSyncModeChanged();
    void onOpenFileClicked();
    void onPlayClicked();
    void onPauseClicked();
    void onStopClicked();
    void onStartSystemCaptureClicked();
    void onResetDelaysClicked();
    void onSliderSeek(int position);
    void onDeviceControlChanged(int rowIndex);
    void onAvLipSyncSliderChanged();
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
    bool isUpdatingDeviceList_{false};
    double currentDurationMs_{0.0};

    // Left column: Devices, Capture, Per-device controls, Telemetry, Engine
    HWND hGrpDevices_{nullptr};
    HWND hListDevices_{nullptr};
    HWND hBtnRefreshDevices_{nullptr};
    HWND hBtnSystemCapture_{nullptr};
    HWND hBtnStopAll_{nullptr};

    HWND hGrpPerDevice_{nullptr};
    std::array<DeviceControlRow, kMaxDeviceControls> deviceRows_;
    HWND hBtnResetDelays_{nullptr};
    HWND hLblDelayTip_{nullptr};

    HWND hGrpLiveSync_{nullptr};
    HWND hListLiveSync_{nullptr};

    HWND hGrpEngine_{nullptr};
    HWND hLblSyncMode_{nullptr};
    HWND hCmbSyncMode_{nullptr};
    HWND hLblSystemTel_{nullptr};

    // Right column: Embedded Video, Media Controls, A/V Telemetry
    HWND hGrpMedia_{nullptr};
    HWND hVideoPanel_{nullptr};
    HWND hBtnOpenFile_{nullptr};
    HWND hLblMediaPath_{nullptr};
    HWND hBtnPlay_{nullptr};
    HWND hBtnPause_{nullptr};
    HWND hBtnStop_{nullptr};
    HWND hSliderProgress_{nullptr};
    HWND hLblTimeProgress_{nullptr};
    HWND hLblAvLipSync_{nullptr};
    HWND hSliderAvLipSync_{nullptr};
    HWND hLblAvLipSyncVal_{nullptr};
    HWND hLblAvTip_{nullptr};

    HWND hGrpMediaTel_{nullptr};
    HWND hLblMediaTel_{nullptr};

    HWND hStatusBar_{nullptr};

    UINT_PTR timerId_{1001};
};

} // namespace syncwave
