#requires -Version 5.1
<#
    Build KeyForge for Windows x64.

        powershell -ExecutionPolicy Bypass -File packaging\windows\build.ps1

    Steps:
      1. install the build requirements (numpy, SoundCard, PyInstaller)
      2. fetch libusb/hidapi's Windows release for hidapi.dll
      3. fetch the wchisp release for wchisp.exe, so flashing needs no Rust
      4. build dist\windows\KeyForge.exe with PyInstaller
      5. build dist\windows\KeyForge-Setup-<version>.exe with Inno Setup
      6. zip the portable copy

    Everything the installed app needs is inside the bundle: Python, numpy,
    SoundCard, hidapi.dll and wchisp.exe.  The only thing it cannot carry is the
    WinUSB driver that wchisp needs for the bootloader (see docs/WINDOWS.md).

    -Version         version string used for the installer name (default 1.0.0)
    -HidapiVersion   hidapi release to fetch (default 0.16.0)
    -WchispVersion   wchisp release to fetch (default 0.3.0)
    -NoClean         reuse the PyInstaller cache for a faster rebuild
    -SkipInstaller   stop after the bare exe
#>
param(
    [string]$Version = "1.0.0",
    [string]$HidapiVersion = "0.16.0",
    [string]$WchispVersion = "0.3.0",
    [switch]$NoClean,
    [switch]$SkipInstaller
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = (Resolve-Path (Join-Path $here "..\..")).Path
Set-Location $root

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) { throw "python is not on PATH (install Python 3.10+ for Windows)" }

Write-Host "== python" (python --version)
python -m pip install --disable-pip-version-check -q -r (Join-Path $here "requirements-build.txt")

function FetchReleaseFile {
    param([string]$Url, [string]$ZipName, [string]$InnerPath, [string]$Destination)
    if (Test-Path $Destination) { return }
    Write-Host "== fetching $ZipName"
    $zip = Join-Path $env:TEMP $ZipName
    Invoke-WebRequest -Uri $Url -OutFile $zip -UseBasicParsing
    $unpack = Join-Path $env:TEMP ([IO.Path]::GetFileNameWithoutExtension($ZipName))
    if (Test-Path $unpack) { Remove-Item -Recurse -Force $unpack }
    Expand-Archive -Path $zip -DestinationPath $unpack -Force
    Copy-Item (Join-Path $unpack $InnerPath) $Destination
}

FetchReleaseFile `
    -Url "https://github.com/libusb/hidapi/releases/download/hidapi-$HidapiVersion/hidapi-win.zip" `
    -ZipName "hidapi-win-$HidapiVersion.zip" `
    -InnerPath "x64\hidapi.dll" `
    -Destination (Join-Path $here "hidapi.dll")

FetchReleaseFile `
    -Url "https://github.com/ch32-rs/wchisp/releases/download/v$WchispVersion/wchisp-v$WchispVersion-win-x64.zip" `
    -ZipName "wchisp-v$WchispVersion-win-x64.zip" `
    -InnerPath "wchisp-win-x64\wchisp.exe" `
    -Destination (Join-Path $here "wchisp.exe")

$wchispLicence = Join-Path $here "wchisp-LICENSE.txt"
if (-not (Test-Path $wchispLicence)) {
    Write-Host "== fetching wchisp licence"
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/ch32-rs/wchisp/v$WchispVersion/LICENSE" `
        -OutFile $wchispLicence -UseBasicParsing
}

Write-Host "== pyinstaller"
$pyi = @("--noconfirm", "--distpath", "dist\windows",
         "--workpath", "build\pyinstaller-windows")
if (-not $NoClean) { $pyi += "--clean" }
python -m PyInstaller @pyi (Join-Path $here "keyforge.spec")
if ($LASTEXITCODE -ne 0) { throw "PyInstaller failed ($LASTEXITCODE)" }

$exe = Join-Path $root "dist\windows\KeyForge.exe"
if (-not (Test-Path $exe)) { throw "PyInstaller did not produce $exe" }
Write-Host ("== exe   {0}  ({1:N1} MB)" -f $exe, ((Get-Item $exe).Length / 1MB))

if ($SkipInstaller) { exit 0 }

$iscc = @(
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) {
    Write-Host "== installing Inno Setup"
    choco install innosetup -y --no-progress | Out-Null
    $iscc = @(
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        "C:\Program Files\Inno Setup 6\ISCC.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $iscc) { throw "Inno Setup (ISCC.exe) was not found" }

Write-Host "== installer (Inno Setup)"
& $iscc "/DAppVersion=$Version" (Join-Path $here "keyforge.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC failed ($LASTEXITCODE)" }

$setup = Join-Path $root "dist\windows\KeyForge-Setup-$Version.exe"
if (-not (Test-Path $setup)) { throw "ISCC did not produce $setup" }
Write-Host ("== setup {0}  ({1:N1} MB)" -f $setup, ((Get-Item $setup).Length / 1MB))

Write-Host "== portable zip"
$portable = Join-Path $root "dist\windows\KeyForge-$Version-portable.zip"
if (Test-Path $portable) { Remove-Item $portable }
$stage = Join-Path $env:TEMP "keyforge-portable"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item $exe $stage
Copy-Item (Join-Path $here "wchisp.exe") $stage
Copy-Item (Join-Path $root "README.md") $stage
Copy-Item (Join-Path $root "LICENSE") $stage
Copy-Item (Join-Path $root "docs\WINDOWS.md") $stage
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $portable
Write-Host ("== zip   {0}  ({1:N1} MB)" -f $portable, ((Get-Item $portable).Length / 1MB))
