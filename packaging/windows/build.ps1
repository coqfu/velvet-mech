#requires -Version 5.1
<#
    Build KeyForge.exe on Windows x64.

    Run from anywhere:

        powershell -ExecutionPolicy Bypass -File packaging\windows\build.ps1

    It installs the build requirements, downloads libusb/hidapi's Windows
    release for hidapi.dll (unless one is already here), then runs PyInstaller.
    The result is dist\windows\KeyForge.exe - a single file that needs no Python.

    -HidapiVersion   which hidapi release to fetch (default 0.16.0)
    -NoClean         reuse the PyInstaller cache for a faster rebuild
#>
param(
    [string]$HidapiVersion = "0.16.0",
    [switch]$NoClean
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = (Resolve-Path (Join-Path $here "..\..")).Path
Set-Location $root

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) { throw "python is not on PATH (install Python 3.10+ for Windows)" }

Write-Host "== python" (python --version)
python -m pip install --disable-pip-version-check -q -r (Join-Path $here "requirements-build.txt")

$dll = Join-Path $here "hidapi.dll"
if (-not (Test-Path $dll)) {
    Write-Host "== hidapi $HidapiVersion"
    $zip = Join-Path $env:TEMP "hidapi-win-$HidapiVersion.zip"
    $url = "https://github.com/libusb/hidapi/releases/download/hidapi-$HidapiVersion/hidapi-win.zip"
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    $unpack = Join-Path $env:TEMP "hidapi-win-$HidapiVersion"
    if (Test-Path $unpack) { Remove-Item -Recurse -Force $unpack }
    Expand-Archive -Path $zip -DestinationPath $unpack -Force
    Copy-Item (Join-Path $unpack "x64\hidapi.dll") $dll
}

Write-Host "== pyinstaller"
$pyi = @("--noconfirm", "--distpath", "dist\windows",
         "--workpath", "build\pyinstaller-windows")
if (-not $NoClean) { $pyi += "--clean" }
python -m PyInstaller @pyi (Join-Path $here "keyforge.spec")
if ($LASTEXITCODE -ne 0) { throw "PyInstaller failed ($LASTEXITCODE)" }

$exe = Join-Path $root "dist\windows\KeyForge.exe"
if (-not (Test-Path $exe)) { throw "PyInstaller did not produce $exe" }

$size = (Get-Item $exe).Length / 1MB
Write-Host ("== ok  {0}  ({1:N1} MB)" -f $exe, $size)
Write-Host "   Run it and the UI opens at http://127.0.0.1:8765/"
