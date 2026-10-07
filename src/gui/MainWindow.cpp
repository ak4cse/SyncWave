#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "MainWindow.h"
#include <commctrl.h>
#include <commdlg.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace syncwave {

static void setListViewItemText(HWND hList, int iItem, int iSubItem, const std::wstring& text) {
    LVITEMW lvi = {};
    lvi.iSubItem = iSubItem;
    lvi.pszText = const_cast<LPWSTR>(text.c_str());
    SendMessageW(hList, LVM_SETITEMTEXTW, static_cast<WPARAM>(iItem), reinterpret_cast<LPARAM>(&lvi));
}

// Control IDs
enum ControlId {
    ID_BTN_REFRESH_DEVICES = 101,
    ID_LIST_DEVICES        = 102,
    ID_CMB_SYNC_MODE       = 103,
    ID_BTN_OPEN_FILE       = 104,
    ID_BTN_PLAY            = 105,
    ID_BTN_PAUSE           = 106,
    ID_BTN_STOP            = 107,
    ID_SLIDER_PROGRESS     = 108,
    ID_LIST_LIVE_SYNC      = 109,
    ID_TIMER_TELEMETRY     = 1001
};

static std::string formatTime(double ms) {
    if (ms < 0.0) ms = 0.0;
    int totalSec = static_cast<int>(ms / 1000.0);
    int minutes = totalSec / 60;
    int seconds = totalSec % 60;
    int hours = minutes / 60;
    minutes = minutes % 60;

    std::ostringstream ss;
    if (hours > 0) {
        ss << std::setfill('0') << std::setw(2) << hours << ":";
    }
    ss << std::setfill('0') << std::setw(2) << minutes << ":"
       << std::setfill('0') << std::setw(2) << seconds;
    return ss.str();
}

MainWindow::MainWindow(HINSTANCE hInstance, std::shared_ptr<ISyncWaveEngine> engine)
    : hInstance_(hInstance), engine_(std::move(engine))
{
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);
}

MainWindow::~MainWindow() {
    if (timerId_ && hWnd_) {
        KillTimer(hWnd_, timerId_);
    }
    if (hFont_) DeleteObject(hFont_);
    if (hFontBold_) DeleteObject(hFontBold_);
    if (hFontMono_) DeleteObject(hFontMono_);
}

bool MainWindow::create(int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"SyncWaveMainWindowClass";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = MainWindow::WndProc;
    wc.hInstance = hInstance_;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    RegisterClassExW(&wc);

    hWnd_ = CreateWindowExW(
        0,
        CLASS_NAME,
        L"SyncWave — Multi-Device Synchronized Audio",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 980, 780,
        nullptr,
        nullptr,
        hInstance_,
        this
    );

    if (!hWnd_) return false;

    ShowWindow(hWnd_, nCmdShow);
    UpdateWindow(hWnd_);

    // Set 10 Hz telemetry polling timer (100 ms)
    SetTimer(hWnd_, ID_TIMER_TELEMETRY, 100, nullptr);

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
        CREATESTRUCTW* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<MainWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->handleMessage(hWnd, message, wParam, lParam);
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

void MainWindow::updateDpiFont(HWND /*hWnd*/) {
    if (!hFont_) {
        hFont_ = CreateFontW(
            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
    }
    if (!hFontBold_) {
        hFontBold_ = CreateFontW(
            -13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
    }
    if (!hFontMono_) {
        hFontMono_ = CreateFontW(
            -12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas"
        );
    }
}

void MainWindow::setupControls(HWND hWnd) {
    updateDpiFont(hWnd);

    // 1. Devices Group
    hGrpDevices_ = CreateWindowW(L"BUTTON", L" Output Audio Devices ",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        15, 10, 460, 215, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hGrpDevices_, WM_SETFONT, (WPARAM)hFontBold_, TRUE);

    hListDevices_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
        25, 32, 440, 155, hWnd, (HMENU)ID_LIST_DEVICES, hInstance_, nullptr);
    SendMessageW(hListDevices_, WM_SETFONT, (WPARAM)hFont_, TRUE);
    ListView_SetExtendedListViewStyle(hListDevices_, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<LPWSTR>(L"Output Endpoint");
    col.cx = 280;
    ListView_InsertColumn(hListDevices_, 0, &col);

    col.pszText = const_cast<LPWSTR>(L"Status");
    col.cx = 135;
    ListView_InsertColumn(hListDevices_, 1, &col);

    hBtnRefreshDevices_ = CreateWindowW(L"BUTTON", L"Refresh Devices",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        25, 192, 120, 26, hWnd, (HMENU)ID_BTN_REFRESH_DEVICES, hInstance_, nullptr);
    SendMessageW(hBtnRefreshDevices_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    // 2. Synchronization Group
    hGrpSync_ = CreateWindowW(L"BUTTON", L" Synchronization Mode ",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        490, 10, 460, 215, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hGrpSync_, WM_SETFONT, (WPARAM)hFontBold_, TRUE);

    hLblSyncMode_ = CreateWindowW(L"STATIC", L"Sync Algorithm Mode:",
        WS_CHILD | WS_VISIBLE,
        505, 35, 150, 20, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hLblSyncMode_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hCmbSyncMode_ = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        505, 58, 260, 120, hWnd, (HMENU)ID_CMB_SYNC_MODE, hInstance_, nullptr);
    SendMessageW(hCmbSyncMode_, WM_SETFONT, (WPARAM)hFont_, TRUE);
    SendMessageW(hCmbSyncMode_, CB_ADDSTRING, 0, (LPARAM)L"Adaptive (Micro-Resampling Drift Correction)");
    SendMessageW(hCmbSyncMode_, CB_ADDSTRING, 0, (LPARAM)L"Software (Static Driver Latency Alignment)");
    SendMessageW(hCmbSyncMode_, CB_ADDSTRING, 0, (LPARAM)L"None (Direct Concurrent Dispatch)");
    SendMessageW(hCmbSyncMode_, CB_SETCURSEL, 0, 0); // Default Adaptive

    HWND hLblSyncDesc = CreateWindowW(L"STATIC",
        L"Adaptive mode dynamically compensates for independent hardware crystal drift via continuous sinc micro-resampling (slew limited to +/-100 ppm).\n\n"
        L"Endpoints are aligned to the slowest output buffer (Bluetooth A2DP transport buffer + WASAPI queue).",
        WS_CHILD | WS_VISIBLE,
        505, 95, 430, 95, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hLblSyncDesc, WM_SETFONT, (WPARAM)hFont_, TRUE);

    // 3. Media Controls Group
    hGrpMedia_ = CreateWindowW(L"BUTTON", L" Media Player & Controls ",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        15, 235, 935, 140, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hGrpMedia_, WM_SETFONT, (WPARAM)hFontBold_, TRUE);

    hBtnOpenFile_ = CreateWindowW(L"BUTTON", L"Open File...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        25, 260, 95, 28, hWnd, (HMENU)ID_BTN_OPEN_FILE, hInstance_, nullptr);
    SendMessageW(hBtnOpenFile_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hLblMediaPath_ = CreateWindowW(L"STATIC", L"No media file loaded. Please click 'Open File...' to select video or audio.",
        WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
        130, 265, 800, 20, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hLblMediaPath_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hBtnPlay_ = CreateWindowW(L"BUTTON", L"▶ Play",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        25, 296, 85, 28, hWnd, (HMENU)ID_BTN_PLAY, hInstance_, nullptr);
    SendMessageW(hBtnPlay_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hBtnPause_ = CreateWindowW(L"BUTTON", L"⏸ Pause",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        118, 296, 85, 28, hWnd, (HMENU)ID_BTN_PAUSE, hInstance_, nullptr);
    SendMessageW(hBtnPause_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hBtnStop_ = CreateWindowW(L"BUTTON", L"■ Stop",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        211, 296, 85, 28, hWnd, (HMENU)ID_BTN_STOP, hInstance_, nullptr);
    SendMessageW(hBtnStop_, WM_SETFONT, (WPARAM)hFont_, TRUE);

    hSliderProgress_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_NOTICKS,
        310, 296, 480, 28, hWnd, (HMENU)ID_SLIDER_PROGRESS, hInstance_, nullptr);
    SendMessageW(hSliderProgress_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));

    hLblTimeProgress_ = CreateWindowW(L"STATIC", L"00:00 / 00:00",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        800, 301, 130, 20, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hLblTimeProgress_, WM_SETFONT, (WPARAM)hFontMono_, TRUE);

    // 4. Live Synchronization Telemetry Group
    hGrpLiveSync_ = CreateWindowW(L"BUTTON", L" Live Output Synchronization Telemetry ",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        15, 385, 935, 175, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hGrpLiveSync_, WM_SETFONT, (WPARAM)hFontBold_, TRUE);

    hListLiveSync_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
        25, 410, 915, 140, hWnd, (HMENU)ID_LIST_LIVE_SYNC, hInstance_, nullptr);
    SendMessageW(hListLiveSync_, WM_SETFONT, (WPARAM)hFont_, TRUE);
    ListView_SetExtendedListViewStyle(hListLiveSync_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    // Live columns
    const struct { const wchar_t* title; int width; } liveCols[] = {
        { L"Endpoint Name", 230 },
        { L"Sync State", 105 },
        { L"Drift State", 95 },
        { L"Phase Error", 90 },
        { L"Drift Rate", 85 },
        { L"Rate Correction", 105 },
        { L"Confidence", 85 },
        { L"Queue / Underruns", 105 }
    };
    for (int i = 0; i < 8; ++i) {
        LVCOLUMNW c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = const_cast<LPWSTR>(liveCols[i].title);
        c.cx = liveCols[i].width;
        ListView_InsertColumn(hListLiveSync_, i, &c);
    }

    // 5. Media & System Telemetry Group
    hGrpMediaTel_ = CreateWindowW(L"BUTTON", L" Audio/Video Alignment & Playback ",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        15, 570, 460, 135, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hGrpMediaTel_, WM_SETFONT, (WPARAM)hFontBold_, TRUE);

    hLblMediaTel_ = CreateWindowW(L"STATIC",
        L"Media Time:       0.0 ms\n"
        L"Video Frame:      0.0 ms\n"
        L"Audio Master:     0.0 ms\n"
        L"A/V Offset:       +0.0 ms (LOCKED)\n"
        L"Playback State:   Stopped (Rate: 1.0x)",
        WS_CHILD | WS_VISIBLE,
        30, 595, 430, 95, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hLblMediaTel_, WM_SETFONT, (WPARAM)hFontMono_, TRUE);

    hGrpSystem_ = CreateWindowW(L"BUTTON", L" Engine & System Performance ",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        490, 570, 460, 135, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hGrpSystem_, WM_SETFONT, (WPARAM)hFontBold_, TRUE);

    hLblSystemTel_ = CreateWindowW(L"STATIC",
        L"Engine State:     Idle\n"
        L"Active Source:    None\n"
        L"Master Bus:       48000 Hz Stereo Float32\n"
        L"Process CPU:      0.0 %\n"
        L"Working Set:      0.0 MB | Uptime: 0s",
        WS_CHILD | WS_VISIBLE,
        505, 595, 430, 95, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hLblSystemTel_, WM_SETFONT, (WPARAM)hFontMono_, TRUE);

    // Status bar
    hStatusBar_ = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hStatusBar_, WM_SETFONT, (WPARAM)hFont_, TRUE);
    SendMessageW(hStatusBar_, SB_SETTEXTW, 0, (LPARAM)L"Ready. Select output devices and load a media file to start.");

    populateDeviceList();
}

void MainWindow::populateDeviceList() {
    ListView_DeleteAllItems(hListDevices_);
    availableDevices_ = engine_->enumerateOutputDevices(true);

    for (size_t i = 0; i < availableDevices_.size(); ++i) {
        const auto& dev = availableDevices_[i];
        std::wstring nameW(dev.name.begin(), dev.name.end());
        if (dev.isDefault) {
            nameW += L" [Default]";
        }

        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(nameW.c_str());
        ListView_InsertItem(hListDevices_, &item);

        std::wstring statusW = dev.isActive ? L"Ready" : L"Unavailable";
        setListViewItemText(hListDevices_, static_cast<int>(i), 1, statusW);

        // Default to checked for first 2 devices if not yet configured
        if (i < 2) {
            ListView_SetCheckState(hListDevices_, static_cast<int>(i), TRUE);
        }
    }
}

void MainWindow::onSyncModeChanged() {
    int sel = static_cast<int>(SendMessageW(hCmbSyncMode_, CB_GETCURSEL, 0, 0));
    if (sel == 0) {
        engine_->setSyncMode("adaptive");
        SendMessageW(hStatusBar_, SB_SETTEXTW, 0, (LPARAM)L"Sync Mode set to: Adaptive (closed-loop micro-resampling)");
    } else if (sel == 1) {
        engine_->setSyncMode("software");
        SendMessageW(hStatusBar_, SB_SETTEXTW, 0, (LPARAM)L"Sync Mode set to: Software (static delay alignment)");
    } else {
        engine_->setSyncMode("none");
        SendMessageW(hStatusBar_, SB_SETTEXTW, 0, (LPARAM)L"Sync Mode set to: None (direct output dispatch)");
    }
}

void MainWindow::onOpenFileClicked() {
    wchar_t szFile[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = hWnd_;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(szFile[0]);
    ofn.lpstrFilter = L"Media Files (*.mp4;*.mkv;*.mp3;*.wav;*.avi;*.flac)\0*.mp4;*.mkv;*.mp3;*.wav;*.avi;*.flac\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        std::wstring ws(szFile);
        currentMediaPath_ = std::string(ws.begin(), ws.end());
        SetWindowTextW(hLblMediaPath_, szFile);
        SendMessageW(hStatusBar_, SB_SETTEXTW, 0, (LPARAM)L"Loaded media file. Click Play to start synchronized playback.");
    }
}

void MainWindow::onPlayClicked() {
    if (isPaused_) {
        engine_->resume();
        isPaused_ = false;
        SendMessageW(hStatusBar_, SB_SETTEXTW, 0, (LPARAM)L"Playback resumed.");
        return;
    }

    if (currentMediaPath_.empty()) {
        MessageBoxW(hWnd_, L"Please click 'Open File...' to select a media file first.", L"No Media File", MB_ICONWARNING);
        return;
    }

    // Collect checked devices
    selectedDeviceIds_.clear();
    int count = ListView_GetItemCount(hListDevices_);
    for (int i = 0; i < count; ++i) {
        if (ListView_GetCheckState(hListDevices_, i)) {
            if (i < static_cast<int>(availableDevices_.size())) {
                selectedDeviceIds_.push_back(availableDevices_[i].id);
            }
        }
    }

    if (selectedDeviceIds_.empty()) {
        MessageBoxW(hWnd_, L"Please select at least one output device.", L"No Output Selected", MB_ICONWARNING);
        return;
    }

    onSyncModeChanged();

    bool ok = engine_->startMedia(currentMediaPath_, selectedDeviceIds_);
    if (!ok) {
        MessageBoxW(hWnd_, L"Unable to start playback. Check the selected devices and media file.", L"Playback Error", MB_ICONERROR);
        SendMessageW(hStatusBar_, SB_SETTEXTW, 0, (LPARAM)L"Error: Playback failed to start.");
    } else {
        SendMessageW(hStatusBar_, SB_SETTEXTW, 0, (LPARAM)L"Synchronized playback running across selected outputs.");
    }
}

void MainWindow::onPauseClicked() {
    if (engine_->isRunning()) {
        if (!isPaused_) {
            engine_->pause();
            isPaused_ = true;
            SendMessageW(hStatusBar_, SB_SETTEXTW, 0, (LPARAM)L"Playback paused.");
        } else {
            engine_->resume();
            isPaused_ = false;
            SendMessageW(hStatusBar_, SB_SETTEXTW, 0, (LPARAM)L"Playback resumed.");
        }
    }
}

void MainWindow::onStopClicked() {
    engine_->stop();
    isPaused_ = false;
    SendMessageW(hStatusBar_, SB_SETTEXTW, 0, (LPARAM)L"Playback stopped.");
}

void MainWindow::onSliderSeek(int position) {
    if (currentDurationMs_ > 0.0) {
        double targetMs = (static_cast<double>(position) / 1000.0) * currentDurationMs_;
        engine_->seekMedia(targetMs);
    }
}

void MainWindow::onTimerTick() {
    auto diag = engine_->getDiagnostics();
    updateTelemetryDisplay(diag);
}

void MainWindow::updateTelemetryDisplay(const FullDiagnosticsSnapshot& diag) {
    // 1. Update Media Progress
    if (diag.media.durationMs > 0.0) {
        currentDurationMs_ = diag.media.durationMs;
        if (!isUserSeeking_) {
            int sliderPos = static_cast<int>((diag.media.mediaPositionMs / diag.media.durationMs) * 1000.0);
            SendMessageW(hSliderProgress_, TBM_SETPOS, TRUE, sliderPos);
        }
        std::string timeStr = formatTime(diag.media.mediaPositionMs) + " / " + formatTime(diag.media.durationMs);
        std::wstring timeW(timeStr.begin(), timeStr.end());
        SetWindowTextW(hLblTimeProgress_, timeW.c_str());
    } else {
        if (!diag.isEngineRunning && !isUserSeeking_) {
            SendMessageW(hSliderProgress_, TBM_SETPOS, TRUE, 0);
            SetWindowTextW(hLblTimeProgress_, L"00:00 / 00:00");
        }
    }

    // 2. Update Live Synchronization ListView
    int existingRows = ListView_GetItemCount(hListLiveSync_);
    if (existingRows != static_cast<int>(diag.endpoints.size())) {
        ListView_DeleteAllItems(hListLiveSync_);
        for (size_t i = 0; i < diag.endpoints.size(); ++i) {
            LVITEMW item = {};
            item.mask = LVIF_TEXT;
            item.iItem = static_cast<int>(i);
            ListView_InsertItem(hListLiveSync_, &item);
        }
    }

    for (size_t i = 0; i < diag.endpoints.size(); ++i) {
        const auto& ep = diag.endpoints[i];
        int row = static_cast<int>(i);

        // Name
        std::wstring nameW(ep.friendlyName.begin(), ep.friendlyName.end());
        setListViewItemText(hListLiveSync_, row, 0, nameW);

        // Sync State
        std::wstring syncW = (ep.syncState == SyncState::PhysicallyCalibrated) ? L"PhysCalibrated" :
                             (ep.syncState == SyncState::SoftwareCalibrated) ? L"SwCalibrated" :
                             (ep.syncState == SyncState::Manual) ? L"Manual" : L"Disabled";
        setListViewItemText(hListLiveSync_, row, 1, syncW);

        // Drift State
        std::wstring driftW(ep.driftState.begin(), ep.driftState.end());
        setListViewItemText(hListLiveSync_, row, 2, driftW);

        // Phase Error
        std::ostringstream ssPhase;
        ssPhase << std::showpos << std::fixed << std::setprecision(1) << ep.phaseErrorMs << " ms";
        std::string sPhase = ssPhase.str();
        std::wstring wPhase(sPhase.begin(), sPhase.end());
        setListViewItemText(hListLiveSync_, row, 3, wPhase);

        // Drift Rate
        std::ostringstream ssDrift;
        ssDrift << std::showpos << std::fixed << std::setprecision(1) << ep.driftPpm << " ppm";
        std::string sDrift = ssDrift.str();
        std::wstring wDrift(sDrift.begin(), sDrift.end());
        setListViewItemText(hListLiveSync_, row, 4, wDrift);

        // Rate Correction
        std::ostringstream ssCorr;
        ssCorr << std::showpos << std::fixed << std::setprecision(1) << ep.activeRateAdjustmentPpm << " ppm";
        std::string sCorr = ssCorr.str();
        std::wstring wCorr(sCorr.begin(), sCorr.end());
        setListViewItemText(hListLiveSync_, row, 5, wCorr);

        // Confidence
        std::ostringstream ssConf;
        ssConf << std::fixed << std::setprecision(0) << (ep.driftConfidence * 100.0) << " %";
        std::string sConf = ssConf.str();
        std::wstring wConf(sConf.begin(), sConf.end());
        setListViewItemText(hListLiveSync_, row, 6, wConf);

        // Queue / Underruns
        std::ostringstream ssQ;
        ssQ << ep.queueFrames << " (u:" << ep.queueUnderruns << ")";
        std::string sQ = ssQ.str();
        std::wstring wQ(sQ.begin(), sQ.end());
        setListViewItemText(hListLiveSync_, row, 7, wQ);
    }

    // 3. Update Media Telemetry text
    std::ostringstream ssMedia;
    ssMedia << "Media Time:       " << std::fixed << std::setprecision(1) << diag.media.mediaPositionMs << " ms\n"
            << "Video Frame:      " << diag.media.videoPositionMs << " ms\n"
            << "Audio Master:     " << diag.media.audioMasterPositionMs << " ms\n"
            << "A/V Offset:       " << std::showpos << std::fixed << std::setprecision(1) << diag.media.audioVideoOffsetMs << " ms" << std::noshowpos
            << (diag.media.isSynchronized ? " (LOCKED)\n" : " (MEASURING)\n")
            << "Playback State:   " << diag.media.playbackState << " (Rate: " << diag.media.playbackRate << "x)";
    std::string sMedia = ssMedia.str();
    std::wstring wMedia(sMedia.begin(), sMedia.end());
    SetWindowTextW(hLblMediaTel_, wMedia.c_str());

    // 4. Update System Telemetry text
    std::ostringstream ssSys;
    ssSys << "Engine State:     " << (diag.isEngineRunning ? "Running" : "Idle") << "\n"
          << "Active Source:    " << diag.activeSource << "\n"
          << "Master Bus:       " << diag.masterSampleRate << " Hz Stereo Float32\n"
          << "Process CPU:      " << std::fixed << std::setprecision(1) << diag.system.processCpuPercent << " %\n"
          << "Working Set:      " << std::fixed << std::setprecision(1) << diag.system.memoryUsageMb << " MB | Uptime: " << static_cast<int>(diag.system.uptimeSec) << "s";
    std::string sSys = ssSys.str();
    std::wstring wSys(sSys.begin(), sSys.end());
    SetWindowTextW(hLblSystemTel_, wSys.c_str());
}

LRESULT MainWindow::handleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        setupControls(hWnd);
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_BTN_REFRESH_DEVICES) {
            populateDeviceList();
        } else if (id == ID_CMB_SYNC_MODE && HIWORD(wParam) == CBN_SELCHANGE) {
            onSyncModeChanged();
        } else if (id == ID_BTN_OPEN_FILE) {
            onOpenFileClicked();
        } else if (id == ID_BTN_PLAY) {
            onPlayClicked();
        } else if (id == ID_BTN_PAUSE) {
            onPauseClicked();
        } else if (id == ID_BTN_STOP) {
            onStopClicked();
        }
        return 0;
    }

    case WM_HSCROLL: {
        HWND hScroll = reinterpret_cast<HWND>(lParam);
        if (hScroll == hSliderProgress_) {
            int code = LOWORD(wParam);
            if (code == TB_THUMBTRACK || code == TB_THUMBPOSITION) {
                isUserSeeking_ = true;
            } else if (code == TB_ENDTRACK) {
                int pos = static_cast<int>(SendMessageW(hSliderProgress_, TBM_GETPOS, 0, 0));
                onSliderSeek(pos);
                isUserSeeking_ = false;
            }
        }
        return 0;
    }

    case WM_TIMER:
        if (wParam == ID_TIMER_TELEMETRY) {
            onTimerTick();
        }
        return 0;

    case WM_DESTROY:
        engine_->stop();
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
}

} // namespace syncwave
