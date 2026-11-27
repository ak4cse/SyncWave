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

$IsccCandidates = @(
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe",
    (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
)

$Iscc = $IsccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Iscc) {
    $cmd = Get-Command iscc -ErrorAction SilentlyContinue
    if ($cmd) { $Iscc = $cmd.Source }
}
if (-not $Iscc) {
    throw "ISCC.exe (Inno Setup 6) not found. Install via: winget install JRSoftware.InnoSetup"
}

Write-Host "Building SyncWave-Setup-1.0.0.exe with Inno Setup ($Iscc)..."
& $Iscc (Join-Path $PSScriptRoot "SyncWave.iss")
if ($LASTEXITCODE -ne 0) {
    throw "ISCC.exe failed with exit code $LASTEXITCODE"
}

Write-Host "Installer created: $(Join-Path $DistDir 'SyncWave-Setup-1.0.0.exe')"
