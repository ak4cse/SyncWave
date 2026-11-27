$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Root "build"
$DistDir = Join-Path $Root "dist"
$StageDir = Join-Path $DistDir "stage"

Write-Host "Staging SyncWave binaries and runtime DLLs..."
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null

Copy-Item (Join-Path $BuildDir "syncwave_gui.exe") $StageDir -Force
Copy-Item (Join-Path $BuildDir "syncwave.exe") $StageDir -Force

$RuntimeDlls = @(
    "C:\msys64\ucrt64\bin\WebView2Loader.dll",
    "C:\msys64\ucrt64\bin\libgcc_s_seh-1.dll",
    "C:\msys64\ucrt64\bin\libstdc++-6.dll",
    "C:\msys64\ucrt64\bin\libwinpthread-1.dll",
    "C:\Program Files\VideoLAN\VLC\libvlc.dll",
    "C:\Program Files\VideoLAN\VLC\libvlccore.dll"
)

foreach ($dll in $RuntimeDlls) {
    if (-not (Test-Path $dll)) {
        throw "Required runtime DLL not found: $dll"
    }
    Copy-Item $dll $StageDir -Force
}

$Makensis = "C:\msys64\ucrt64\bin\makensis.exe"
if (-not (Test-Path $Makensis)) {
    throw "makensis.exe not found at $Makensis. Install mingw-w64-ucrt-x86_64-nsis first."
}

Write-Host "Building SyncWave-Setup-1.0.0.exe..."
& $Makensis (Join-Path $PSScriptRoot "SyncWave.nsi")
if ($LASTEXITCODE -ne 0) {
    throw "makensis failed with exit code $LASTEXITCODE"
}

Write-Host "Installer created: $(Join-Path $DistDir 'SyncWave-Setup-1.0.0.exe')"
