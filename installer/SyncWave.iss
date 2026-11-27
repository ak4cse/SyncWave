#define MyAppName "SyncWave"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Ankush"
#define MyAppURL "https://github.com/ak4cse/SyncWave"
#define MyAppExeName "syncwave_gui.exe"

[Setup]
AppId={{8D4F9B32-7B51-4A9E-91F2-9A55C8C7E102}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir=..\dist
OutputBaseFilename=SyncWave-Setup-1.0.0
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\assets\syncwave.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "..\dist\stage\syncwave_gui.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\stage\syncwave.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\stage\WebView2Loader.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\stage\libgcc_s_seh-1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\stage\libstdc++-6.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\stage\libwinpthread-1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\stage\libvlc.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\stage\libvlccore.dll"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
