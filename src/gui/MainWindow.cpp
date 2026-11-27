#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "MainWindow.h"
#include "../windows/ComHelper.h"
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
    ID_BTN_SYSTEM_CAPTURE  = 110,
    ID_BTN_STOP_ALL        = 111,
    ID_BTN_RESET_DELAYS    = 112,
    ID_SLIDER_AV_LIPSYNC   = 113,

    // Per-device controls: base + rowIndex * 10
    ID_DEV_MUTE_BASE       = 200,
    ID_DEV_VOL_BASE        = 201,
    ID_DEV_DELAY_BASE      = 202,

    ID_TIMER_TELEMETRY     = 1001
};

static std::string formatTime(double ms) {
    if (ms < 0.0) ms = 0.0;
    int totalSec = static_cast<int>(ms / 1000.0);
    int hrs = totalSec / 3600;
    int mins = (totalSec % 3600) / 60;
    int secs = totalSec % 60;
    std::ostringstream oss;
    if (hrs > 0) {
        oss << std::setfill('0') << std::setw(2) << hrs << ":";
    }
    oss << std::setfill('0') << std::setw(2) << mins << ":"
        << std::setfill('0') << std::setw(2) << secs;
    return oss.str();
}

MainWindow::MainWindow(HINSTANCE hInstance, std::shared_ptr<ISyncWaveEngine> engine)
    : hInstance_(hInstance), engine_(std::move(engine)) {}

MainWindow::~MainWindow() {
    if (hFont_) DeleteObject(hFont_);
    if (hFontBold_) DeleteObject(hFontBold_);
    if (hFontMono_) DeleteObject(hFontMono_);
}

bool MainWindow::create(int nCmdShow) {
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    const wchar_t CLASS_NAME[] = L"SyncWaveMainWindowClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWindow::WndProc;
    wc.hInstance = hInstance_;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

    RegisterClassW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT rc = {0, 0, 1160, 755};
    AdjustWindowRect(&rc, style, FALSE);

    hWnd_ = CreateWindowExW(
        0,
        CLASS_NAME,
        L"SyncWave — Multi-Device Synchronized Audio & Video",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr,
        nullptr,
        hInstance_,
        this
    );

    if (!hWnd_) return false;

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
            updateDpiFont(hWnd);
            setupControls(hWnd);
            populateDeviceList();
            if (engine_ && hVideoPanel_) {
                engine_->setVideoWindow(hVideoPanel_);
            }
            SetTimer(hWnd, ID_TIMER_TELEMETRY, 250, nullptr);
            return 0;

        case WM_TIMER:
            if (wParam == ID_TIMER_TELEMETRY) {
                onTimerTick();
            }
            return 0;

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            int wmEvent = HIWORD(wParam);

            if (wmId == ID_BTN_REFRESH_DEVICES && wmEvent == BN_CLICKED) {
                populateDeviceList();
            } else if (wmId == ID_BTN_SYSTEM_CAPTURE && wmEvent == BN_CLICKED) {
                onStartSystemCaptureClicked();
            } else if (wmId == ID_BTN_STOP_ALL && wmEvent == BN_CLICKED) {
                onStopClicked();
            } else if (wmId == ID_BTN_RESET_DELAYS && wmEvent == BN_CLICKED) {
                onResetDelaysClicked();
            } else if (wmId == ID_CMB_SYNC_MODE && wmEvent == CBN_SELCHANGE) {
                onSyncModeChanged();
            } else if (wmId == ID_BTN_OPEN_FILE && wmEvent == BN_CLICKED) {
                onOpenFileClicked();
            } else if (wmId == ID_BTN_PLAY && wmEvent == BN_CLICKED) {
                onPlayClicked();
            } else if (wmId == ID_BTN_PAUSE && wmEvent == BN_CLICKED) {
                onPauseClicked();
            } else if (wmId == ID_BTN_STOP && wmEvent == BN_CLICKED) {
                onStopClicked();
            } else if (wmId >= ID_DEV_MUTE_BASE && wmId < ID_DEV_MUTE_BASE + kMaxDeviceControls * 10) {
                int offset = wmId - ID_DEV_MUTE_BASE;
                if (offset % 10 == 0 && wmEvent == BN_CLICKED) {
                    onDeviceControlChanged(offset / 10);
                }
            }
            return 0;
        }

        case WM_HSCROLL: {
            HWND hSlider = reinterpret_cast<HWND>(lParam);
            if (hSlider == hSliderProgress_) {
                int req = LOWORD(wParam);
                if (req == TB_THUMBTRACK || req == TB_THUMBPOSITION) {
                    isUserSeeking_ = true;
                } else if (req == TB_ENDTRACK) {
                    int pos = static_cast<int>(SendMessageW(hSliderProgress_, TBM_GETPOS, 0, 0));
                    onSliderSeek(pos);
                    isUserSeeking_ = false;
                }
            } else if (hSlider == hSliderAvLipSync_) {
                onAvLipSyncSliderChanged();
            } else {
                for (int i = 0; i < kMaxDeviceControls; ++i) {
                    if (hSlider == deviceRows_[i].hSliderVol || hSlider == deviceRows_[i].hSliderDelay) {
                        onDeviceControlChanged(i);
                        break;
                    }
                }
            }
            return 0;
        }

        case WM_NOTIFY: {
            LPNMHDR lpnm = reinterpret_cast<LPNMHDR>(lParam);
            if (lpnm && lpnm->idFrom == ID_LIST_DEVICES && lpnm->code == LVN_ITEMCHANGED) {
                LPNMLISTVIEW pnmv = reinterpret_cast<LPNMLISTVIEW>(lParam);
                if (!isUpdatingDeviceList_ && (pnmv->uChanged & LVIF_STATE)) {
                    if ((pnmv->uNewState ^ pnmv->uOldState) & LVIS_STATEIMAGEMASK) {
                        syncDeviceControlRows();
                    }
                }
            }
            return 0;
        }

        case WM_CLOSE:
            KillTimer(hWnd, ID_TIMER_TELEMETRY);
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

void MainWindow::updateDpiFont(HWND hWnd) {
    HDC hdc = GetDC(hWnd);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(hWnd, hdc);

    int fontHeight = -MulDiv(9, dpi, 72);
    hFont_ = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    hFontBold_ = CreateFontW(fontHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    hFontMono_ = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
}

void MainWindow::setupControls(HWND hWnd) {
    // =========================================================================
    // LEFT COLUMN (x = 15, width = 560)
    // =========================================================================

    // 1. Output Devices & System/Browser Capture
    hGrpDevices_ = CreateWindowW(L"BUTTON", L" Output Audio Devices & System Capture ",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        15, 10, 560, 175, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hGrpDevices_, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold_), TRUE);

    hListDevices_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        25, 32, 540, 110, hWnd, reinterpret_cast<HMENU>(ID_LIST_DEVICES), hInstance_, nullptr);
    SendMessageW(hListDevices_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);
    ListView_SetExtendedListViewStyle(hListDevices_, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<LPWSTR>(L"Output Endpoint");
    col.cx = 390;
    ListView_InsertColumn(hListDevices_, 0, &col);
    col.pszText = const_cast<LPWSTR>(L"Status");
    col.cx = 125;
    ListView_InsertColumn(hListDevices_, 1, &col);

    hBtnRefreshDevices_ = CreateWindowW(L"BUTTON", L"Refresh",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        25, 149, 85, 28, hWnd, reinterpret_cast<HMENU>(ID_BTN_REFRESH_DEVICES), hInstance_, nullptr);
    SendMessageW(hBtnRefreshDevices_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    hBtnSystemCapture_ = CreateWindowW(L"BUTTON", L"▶ Sync Browser / System Audio (Auto-Switch)",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        118, 149, 325, 28, hWnd, reinterpret_cast<HMENU>(ID_BTN_SYSTEM_CAPTURE), hInstance_, nullptr);
    SendMessageW(hBtnSystemCapture_, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold_), TRUE);

    hBtnStopAll_ = CreateWindowW(L"BUTTON", L"■ Stop Audio",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        451, 149, 114, 28, hWnd, reinterpret_cast<HMENU>(ID_BTN_STOP_ALL), hInstance_, nullptr);
    SendMessageW(hBtnStopAll_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    // 2. Per-Device Volume & Sync Delay Controls
    hGrpPerDevice_ = CreateWindowW(L"BUTTON", L" Per-Device Volume && Sync Delay Controls ",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        15, 195, 560, 222, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hGrpPerDevice_, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold_), TRUE);

    for (int i = 0; i < kMaxDeviceControls; ++i) {
        int y = 218 + i * 41;
        auto& row = deviceRows_[i];

        row.hLblName = CreateWindowW(L"STATIC", L"(Unassigned)",
            WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS | SS_NOPREFIX,
            25, y + 4, 150, 20, hWnd, nullptr, hInstance_, nullptr);
        SendMessageW(row.hLblName, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

        row.hChkMute = CreateWindowW(L"BUTTON", L"Mute",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            180, y + 2, 52, 22, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_DEV_MUTE_BASE + i * 10)), hInstance_, nullptr);
        SendMessageW(row.hChkMute, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

        row.hSliderVol = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
            234, y, 105, 26, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_DEV_VOL_BASE + i * 10)), hInstance_, nullptr);
        SendMessageW(row.hSliderVol, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(row.hSliderVol, TBM_SETPOS, TRUE, 100);

        row.hLblVol = CreateWindowW(L"STATIC", L"100%",
            WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
            341, y + 4, 38, 20, hWnd, nullptr, hInstance_, nullptr);
        SendMessageW(row.hLblVol, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

        row.hSliderDelay = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
            382, y, 125, 26, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_DEV_DELAY_BASE + i * 10)), hInstance_, nullptr);
        SendMessageW(row.hSliderDelay, TBM_SETRANGE, TRUE, MAKELPARAM(0, 400));
        SendMessageW(row.hSliderDelay, TBM_SETPOS, TRUE, 0);

        row.hLblDelay = CreateWindowW(L"STATIC", L"0 ms",
            WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
            510, y + 4, 55, 20, hWnd, nullptr, hInstance_, nullptr);
        SendMessageW(row.hLblDelay, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);
    }

    hLblDelayTip_ = CreateWindowW(L"STATIC", L"Tip: Delay wired speakers (~180-220 ms) so they match Bluetooth earphones.",
        WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
        25, 386, 435, 20, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hLblDelayTip_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    hBtnResetDelays_ = CreateWindowW(L"BUTTON", L"Reset Delays",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        465, 382, 100, 26, hWnd, reinterpret_cast<HMENU>(ID_BTN_RESET_DELAYS), hInstance_, nullptr);
    SendMessageW(hBtnResetDelays_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    // 3. Live Output Synchronization Telemetry
    hGrpLiveSync_ = CreateWindowW(L"BUTTON", L" Live Output Synchronization Telemetry ",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        15, 425, 560, 165, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hGrpLiveSync_, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold_), TRUE);

    hListLiveSync_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        25, 447, 540, 133, hWnd, reinterpret_cast<HMENU>(ID_LIST_LIVE_SYNC), hInstance_, nullptr);
    SendMessageW(hListLiveSync_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);
    ListView_SetExtendedListViewStyle(hListLiveSync_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    const wchar_t* telCols[] = {L"Endpoint Name", L"Sync State", L"Drift", L"Phase Err", L"Delay", L"Underruns"};
    int telWidths[] = {175, 78, 75, 72, 65, 70};
    for (int i = 0; i < 6; ++i) {
        LVCOLUMNW c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = const_cast<LPWSTR>(telCols[i]);
        c.cx = telWidths[i];
        ListView_InsertColumn(hListLiveSync_, i, &c);
    }

    // 4. Engine & Sync Algorithm Mode
    hGrpEngine_ = CreateWindowW(L"BUTTON", L" Engine && Synchronization Mode ",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        15, 600, 560, 118, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hGrpEngine_, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold_), TRUE);

    hLblSyncMode_ = CreateWindowW(L"STATIC", L"Sync Mode:",
        WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
        25, 626, 75, 20, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hLblSyncMode_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    hCmbSyncMode_ = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        102, 622, 463, 120, hWnd, reinterpret_cast<HMENU>(ID_CMB_SYNC_MODE), hInstance_, nullptr);
    SendMessageW(hCmbSyncMode_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);
    SendMessageW(hCmbSyncMode_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Adaptive (Micro-Resampling Drift Correction)"));
    SendMessageW(hCmbSyncMode_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Software (Static Driver Latency Alignment)"));
    SendMessageW(hCmbSyncMode_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"None (Direct Concurrent Dispatch)"));
    SendMessageW(hCmbSyncMode_, CB_SETCURSEL, 0, 0);

    hLblSystemTel_ = CreateWindowW(L"STATIC", L"Initializing engine...",
        WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
        25, 654, 540, 56, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hLblSystemTel_, WM_SETFONT, reinterpret_cast<WPARAM>(hFontMono_), TRUE);

    // =========================================================================
    // RIGHT COLUMN (x = 590, width = 555)
    // =========================================================================

    // 5. Embedded Video & Media Player
    hGrpMedia_ = CreateWindowW(L"BUTTON", L" Media Player (Synced Video + Multi-Audio) ",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        590, 10, 555, 580, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hGrpMedia_, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold_), TRUE);

    hVideoPanel_ = CreateWindowW(L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_BLACKRECT,
        602, 34, 531, 358, hWnd, nullptr, hInstance_, nullptr);

    hBtnOpenFile_ = CreateWindowW(L"BUTTON", L"Open File...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        602, 404, 95, 28, hWnd, reinterpret_cast<HMENU>(ID_BTN_OPEN_FILE), hInstance_, nullptr);
    SendMessageW(hBtnOpenFile_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    hLblMediaPath_ = CreateWindowW(L"STATIC", L"Click 'Open File...' to select a movie or audio file.",
        WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS | SS_NOPREFIX,
        705, 409, 428, 20, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hLblMediaPath_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    hBtnPlay_ = CreateWindowW(L"BUTTON", L"▶ Play",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        602, 442, 82, 28, hWnd, reinterpret_cast<HMENU>(ID_BTN_PLAY), hInstance_, nullptr);
    SendMessageW(hBtnPlay_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    hBtnPause_ = CreateWindowW(L"BUTTON", L"⏸ Pause",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        690, 442, 82, 28, hWnd, reinterpret_cast<HMENU>(ID_BTN_PAUSE), hInstance_, nullptr);
    SendMessageW(hBtnPause_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    hBtnStop_ = CreateWindowW(L"BUTTON", L"■ Stop",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        778, 442, 82, 28, hWnd, reinterpret_cast<HMENU>(ID_BTN_STOP), hInstance_, nullptr);
    SendMessageW(hBtnStop_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    hLblTimeProgress_ = CreateWindowW(L"STATIC", L"00:00 / 00:00",
        WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
        875, 447, 258, 20, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hLblTimeProgress_, WM_SETFONT, reinterpret_cast<WPARAM>(hFontMono_), TRUE);

    hSliderProgress_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_NOTICKS,
        602, 478, 531, 26, hWnd, reinterpret_cast<HMENU>(ID_SLIDER_PROGRESS), hInstance_, nullptr);
    SendMessageW(hSliderProgress_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));

    hLblAvLipSync_ = CreateWindowW(L"STATIC", L"Video Lip-Sync:",
        WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
        602, 517, 100, 20, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hLblAvLipSync_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    hSliderAvLipSync_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        704, 513, 305, 26, hWnd, reinterpret_cast<HMENU>(ID_SLIDER_AV_LIPSYNC), hInstance_, nullptr);
    SendMessageW(hSliderAvLipSync_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 700)); // 0..700 mapped to -500..+200 ms
    SendMessageW(hSliderAvLipSync_, TBM_SETPOS, TRUE, 500); // 0 ms default

    hLblAvLipSyncVal_ = CreateWindowW(L"STATIC", L"0 ms",
        WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
        1015, 517, 118, 20, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hLblAvLipSyncVal_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    hLblAvTip_ = CreateWindowW(L"STATIC", L"Tip: Slide Lip-Sync negative (e.g. -200 ms) to advance audio ahead of Bluetooth delay.",
        WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
        602, 548, 531, 34, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hLblAvTip_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);

    // 6. A/V Alignment Telemetry
    hGrpMediaTel_ = CreateWindowW(L"BUTTON", L" Audio/Video Alignment Telemetry ",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        590, 600, 555, 118, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hGrpMediaTel_, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold_), TRUE);

    hLblMediaTel_ = CreateWindowW(L"STATIC", L"Media Time:   0.0 ms\nAudio Master: 0.0 ms\nA/V Offset:   +0.0 ms\nState:        Idle",
        WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
        602, 624, 531, 86, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hLblMediaTel_, WM_SETFONT, reinterpret_cast<WPARAM>(hFontMono_), TRUE);

    // 7. Status Bar
    hStatusBar_ = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, hWnd, nullptr, hInstance_, nullptr);
    SendMessageW(hStatusBar_, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);
    SendMessageW(hStatusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"Ready. Check output devices, then click 'Sync Browser / System Audio' or 'Open File...'."));
}

void MainWindow::populateDeviceList() {
    isUpdatingDeviceList_ = true;
    ListView_DeleteAllItems(hListDevices_);
    availableDevices_ = engine_->enumerateOutputDevices(true);

    for (size_t i = 0; i < availableDevices_.size(); ++i) {
        const auto& dev = availableDevices_[i];
        std::wstring nameW = utf8ToWide(dev.name);
        if (dev.isDefault) {
            nameW += L" [DEFAULT]";
        }

        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = static_cast<int>(i);
        lvi.pszText = const_cast<LPWSTR>(nameW.c_str());
        ListView_InsertItem(hListDevices_, &lvi);

        setListViewItemText(hListDevices_, static_cast<int>(i), 1, L"Active");

        // Auto-check physical outputs (e.g. Realtek + Headphones/Buds), leaving Steam Streaming unchecked for loopback
        bool isVirtualSteam = (dev.name.find("Steam Streaming") != std::string::npos);
        if (!isVirtualSteam) {
            ListView_SetCheckState(hListDevices_, static_cast<int>(i), TRUE);
        }
    }
    isUpdatingDeviceList_ = false;
    syncDeviceControlRows();
}

std::vector<std::string> MainWindow::collectSelectedDeviceIds() const {
    std::vector<std::string> ids;
    int count = ListView_GetItemCount(hListDevices_);
    for (int i = 0; i < count; ++i) {
        if (ListView_GetCheckState(hListDevices_, i)) {
            if (i < static_cast<int>(availableDevices_.size())) {
                ids.push_back(availableDevices_[i].id);
            }
        }
    }
    return ids;
}

void MainWindow::syncDeviceControlRows() {
    std::vector<AudioDevice> checked;
    int count = ListView_GetItemCount(hListDevices_);
    for (int i = 0; i < count; ++i) {
        if (ListView_GetCheckState(hListDevices_, i) && i < static_cast<int>(availableDevices_.size())) {
            checked.push_back(availableDevices_[i]);
        }
    }

    for (int r = 0; r < kMaxDeviceControls; ++r) {
        auto& row = deviceRows_[r];
        if (r < static_cast<int>(checked.size())) {
            std::wstring wName = utf8ToWide(checked[r].name);
            SetWindowTextW(row.hLblName, wName.c_str());
            EnableWindow(row.hChkMute, TRUE);
            EnableWindow(row.hSliderVol, TRUE);
            EnableWindow(row.hSliderDelay, TRUE);
        } else {
            SetWindowTextW(row.hLblName, L"(Unassigned)");
            EnableWindow(row.hChkMute, FALSE);
            EnableWindow(row.hSliderVol, FALSE);
            EnableWindow(row.hSliderDelay, FALSE);
        }
    }
}

void MainWindow::onDeviceControlChanged(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= kMaxDeviceControls) return;

    auto& row = deviceRows_[rowIndex];
    row.muted = (SendMessageW(row.hChkMute, BM_GETCHECK, 0, 0) == BST_CHECKED);
    row.volumePercent = static_cast<int>(SendMessageW(row.hSliderVol, TBM_GETPOS, 0, 0));
    row.delayMs = static_cast<int>(SendMessageW(row.hSliderDelay, TBM_GETPOS, 0, 0));

    std::wstring volStr = std::to_wstring(row.volumePercent) + L"%";
    SetWindowTextW(row.hLblVol, volStr.c_str());

    std::wstring delayStr = std::to_wstring(row.delayMs) + L" ms";
    SetWindowTextW(row.hLblDelay, delayStr.c_str());

    // Push live to engine
    engine_->setDeviceVolume(static_cast<size_t>(rowIndex), static_cast<float>(row.volumePercent) / 100.0f);
    engine_->setDeviceMuted(static_cast<size_t>(rowIndex), row.muted);

    std::vector<double> delays(kMaxDeviceControls, 0.0);
    for (int i = 0; i < kMaxDeviceControls; ++i) {
        delays[i] = static_cast<double>(deviceRows_[i].delayMs);
    }
    engine_->setManualDelays(delays);
}

void MainWindow::onResetDelaysClicked() {
    std::vector<double> delays(kMaxDeviceControls, 0.0);
    for (int i = 0; i < kMaxDeviceControls; ++i) {
        deviceRows_[i].delayMs = 0;
        SendMessageW(deviceRows_[i].hSliderDelay, TBM_SETPOS, TRUE, 0);
        SetWindowTextW(deviceRows_[i].hLblDelay, L"0 ms");
    }
    engine_->setManualDelays(delays);
}

void MainWindow::onAvLipSyncSliderChanged() {
    int pos = static_cast<int>(SendMessageW(hSliderAvLipSync_, TBM_GETPOS, 0, 0));
    int delayMs = pos - 500; // -500 ms .. +200 ms
    std::wstring txt = std::to_wstring(delayMs) + L" ms";
    SetWindowTextW(hLblAvLipSyncVal_, txt.c_str());
    engine_->setAvSyncDelayMs(static_cast<double>(delayMs));
}

void MainWindow::onSyncModeChanged() {
    int sel = static_cast<int>(SendMessageW(hCmbSyncMode_, CB_GETCURSEL, 0, 0));
    if (sel == 0) {
        engine_->setSyncMode("adaptive");
        SendMessageW(hStatusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"Sync Mode: Adaptive (closed-loop micro-resampling)."));
    } else if (sel == 1) {
        engine_->setSyncMode("software");
        SendMessageW(hStatusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"Sync Mode: Software (static latency alignment)."));
    } else {
        engine_->setSyncMode("none");
        SendMessageW(hStatusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"Sync Mode: None (direct concurrent dispatch)."));
    }
}

void MainWindow::onStartSystemCaptureClicked() {
    selectedDeviceIds_ = collectSelectedDeviceIds();
    if (selectedDeviceIds_.empty()) {
        MessageBoxW(hWnd_, L"Please select at least one output device.", L"No Output Selected", MB_ICONWARNING);
        return;
    }

    onSyncModeChanged();
    isPaused_ = false;

    bool ok = engine_->startAutoSystemCapture(selectedDeviceIds_);
    if (!ok) {
        std::string err = engine_->getLastError();
        std::wstring wErr = utf8ToWide(err.empty() ? "Unable to start System/Browser audio capture." : err);
        MessageBoxW(hWnd_, wErr.c_str(), L"System Capture Error", MB_ICONERROR);
        SendMessageW(hStatusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"Error: System audio capture failed."));
    } else {
        SendMessageW(hStatusBar_, SB_SETTEXTW, 0,
            reinterpret_cast<LPARAM>(L"System / Browser audio capture active (Windows default auto-switched; will restore on Stop/Exit)."));
    }
}

void MainWindow::onOpenFileClicked() {
    wchar_t szFile[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd_;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(szFile[0]);
    ofn.lpstrFilter = L"Media Files (*.mp4;*.mkv;*.mp3;*.wav;*.avi;*.flac)\0*.mp4;*.mkv;*.mp3;*.wav;*.avi;*.flac\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        currentMediaPath_ = wideToUtf8(szFile);
        SetWindowTextW(hLblMediaPath_, szFile);
        SendMessageW(hStatusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"Loaded media file. Click Play to start synced video & audio."));
    }
}

void MainWindow::onPlayClicked() {
    if (isPaused_) {
        engine_->resume();
        isPaused_ = false;
        SendMessageW(hStatusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"Playback resumed."));
        return;
    }

    if (currentMediaPath_.empty()) {
        MessageBoxW(hWnd_, L"Please click 'Open File...' to select a media file first.", L"No Media File", MB_ICONWARNING);
        return;
    }

    selectedDeviceIds_ = collectSelectedDeviceIds();
    if (selectedDeviceIds_.empty()) {
        MessageBoxW(hWnd_, L"Please select at least one output device.", L"No Output Selected", MB_ICONWARNING);
        return;
    }

    onSyncModeChanged();
    engine_->setVideoWindow(hVideoPanel_);

    bool ok = engine_->startMedia(currentMediaPath_, selectedDeviceIds_);
    if (!ok) {
        std::string err = engine_->getLastError();
        std::wstring wErr = err.empty() ? L"Unable to start playback. Check the selected devices and media file."
                                        : utf8ToWide("Playback Error: " + err);
        MessageBoxW(hWnd_, wErr.c_str(), L"Playback Error", MB_ICONERROR);
        std::wstring statusMsg = utf8ToWide("Error: " + (err.empty() ? "Playback failed to start." : err));
        SendMessageW(hStatusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(statusMsg.c_str()));
    } else {
        SendMessageW(hStatusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"Synchronized video + multi-output playback running."));
    }
}

void MainWindow::onPauseClicked() {
    if (engine_->isRunning()) {
        if (!isPaused_) {
            engine_->pause();
            isPaused_ = true;
            SendMessageW(hStatusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"Playback paused."));
        } else {
            engine_->resume();
            isPaused_ = false;
            SendMessageW(hStatusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"Playback resumed."));
        }
    }
}

void MainWindow::onStopClicked() {
    engine_->stop();
    isPaused_ = false;
    InvalidateRect(hVideoPanel_, nullptr, TRUE);
    SendMessageW(hStatusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"Stopped. Original Windows default audio device restored."));
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
    int existingCount = ListView_GetItemCount(hListLiveSync_);
    if (existingCount != static_cast<int>(diag.endpoints.size())) {
        ListView_DeleteAllItems(hListLiveSync_);
        for (size_t i = 0; i < diag.endpoints.size(); ++i) {
            LVITEMW lvi = {};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = static_cast<int>(i);
            std::wstring nameW = utf8ToWide(diag.endpoints[i].friendlyName);
            lvi.pszText = const_cast<LPWSTR>(nameW.c_str());
            ListView_InsertItem(hListLiveSync_, &lvi);
        }
    }

    for (size_t i = 0; i < diag.endpoints.size(); ++i) {
        const auto& ep = diag.endpoints[i];
        int row = static_cast<int>(i);

        std::wstringstream ssDrift, ssPhase, ssDelay, ssUnd;
        ssDrift << std::fixed << std::setprecision(1) << ep.filteredDriftPpm << L" ppm";
        ssPhase << std::fixed << std::setprecision(2) << ep.filteredPhaseErrorMs << L" ms";
        ssDelay << std::fixed << std::setprecision(0) << ep.configuredDelayMs << L" ms";
        ssUnd << ep.wasapiUnderruns;

        setListViewItemText(hListLiveSync_, row, 1, utf8ToWide(syncStateToString(ep.syncState)));
        setListViewItemText(hListLiveSync_, row, 2, ssDrift.str());
        setListViewItemText(hListLiveSync_, row, 3, ssPhase.str());
        setListViewItemText(hListLiveSync_, row, 4, ssDelay.str());
        setListViewItemText(hListLiveSync_, row, 5, ssUnd.str());
    }

    currentDurationMs_ = diag.media.durationMs;
    if (!isUserSeeking_ && currentDurationMs_ > 0.0) {
        int sliderPos = static_cast<int>((diag.media.mediaPositionMs / currentDurationMs_) * 1000.0);
        sliderPos = std::max(0, std::min(1000, sliderPos));
        SendMessageW(hSliderProgress_, TBM_SETPOS, TRUE, sliderPos);
    }

    std::string timeStr = formatTime(diag.media.mediaPositionMs) + " / " + formatTime(diag.media.durationMs);
    SetWindowTextW(hLblTimeProgress_, utf8ToWide(timeStr).c_str());

    std::ostringstream ossMedia;
    ossMedia << std::fixed << std::setprecision(1)
             << "Media Time:   " << diag.media.mediaPositionMs << " ms\n"
             << "Audio Master: " << diag.media.audioMasterPositionMs << " ms\n"
             << "A/V Offset:   " << std::showpos << diag.media.audioVideoOffsetMs << std::noshowpos << " ms ("
             << (diag.media.isSynchronized ? "LOCKED" : "MEASURING") << ")\n"
             << "State:        " << diag.media.playbackState << " (Rate: " << diag.media.playbackRate << "x)";
    SetWindowTextW(hLblMediaTel_, utf8ToWide(ossMedia.str()).c_str());

    std::ostringstream ossSys;
    ossSys << std::fixed << std::setprecision(1)
           << "Engine State: " << (diag.isEngineRunning ? "Running" : "Idle")
           << "   |   Active Source: " << diag.activeSource << "\n"
           << "Master Bus:   " << diag.masterSampleRate << " Hz Stereo Float32\n"
           << "Process CPU:  " << diag.system.processCpuPercent << " %   |   Memory: "
           << diag.system.memoryUsageMb << " MB";
    SetWindowTextW(hLblSystemTel_, utf8ToWide(ossSys.str()).c_str());
}

} // namespace syncwave
