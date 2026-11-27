!include "MUI2.nsh"

Name "SyncWave"
OutFile "..\dist\SyncWave-Setup-1.0.0.exe"
InstallDir "$PROGRAMFILES64\SyncWave"
InstallDirRegKey HKLM "Software\SyncWave" "Install_Dir"
RequestExecutionLevel admin
Unicode True

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\syncwave_gui.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch SyncWave now"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "SyncWave (required)"
    SectionIn RO
    SetOutPath "$INSTDIR"

    File "..\dist\stage\syncwave_gui.exe"
    File "..\dist\stage\syncwave.exe"
    File "..\dist\stage\WebView2Loader.dll"
    File "..\dist\stage\libgcc_s_seh-1.dll"
    File "..\dist\stage\libstdc++-6.dll"
    File "..\dist\stage\libwinpthread-1.dll"
    File "..\dist\stage\libvlc.dll"
    File "..\dist\stage\libvlccore.dll"

    WriteRegStr HKLM "Software\SyncWave" "Install_Dir" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SyncWave" "DisplayName" "SyncWave — Multi-Device Audio Sync"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SyncWave" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SyncWave" "DisplayIcon" "$INSTDIR\syncwave_gui.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SyncWave" "Publisher" "SyncWave"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SyncWave" "DisplayVersion" "1.0.0"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SyncWave" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SyncWave" "NoRepair" 1
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Start Menu / Windows Apps Menu shortcut
    CreateDirectory "$SMPROGRAMS\SyncWave"
    CreateShortcut "$SMPROGRAMS\SyncWave\SyncWave.lnk" "$INSTDIR\syncwave_gui.exe" "" "$INSTDIR\syncwave_gui.exe" 0
    CreateShortcut "$SMPROGRAMS\SyncWave\Uninstall SyncWave.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0

    ; Desktop shortcut
    CreateShortcut "$DESKTOP\SyncWave.lnk" "$INSTDIR\syncwave_gui.exe" "" "$INSTDIR\syncwave_gui.exe" 0
SectionEnd

Section "Uninstall"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SyncWave"
    DeleteRegKey HKLM "Software\SyncWave"

    Delete "$DESKTOP\SyncWave.lnk"
    Delete "$SMPROGRAMS\SyncWave\SyncWave.lnk"
    Delete "$SMPROGRAMS\SyncWave\Uninstall SyncWave.lnk"
    RMDir "$SMPROGRAMS\SyncWave"

    Delete "$INSTDIR\syncwave_gui.exe"
    Delete "$INSTDIR\syncwave.exe"
    Delete "$INSTDIR\WebView2Loader.dll"
    Delete "$INSTDIR\libgcc_s_seh-1.dll"
    Delete "$INSTDIR\libstdc++-6.dll"
    Delete "$INSTDIR\libwinpthread-1.dll"
    Delete "$INSTDIR\libvlc.dll"
    Delete "$INSTDIR\libvlccore.dll"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
SectionEnd
