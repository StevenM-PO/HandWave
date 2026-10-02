# Build (if needed) and launch HandWave on Windows using the Qt MinGW kit.
# Usage:  .\scripts\run-windows.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.12.0\mingw_64\bin;$env:PATH"

Push-Location $root
try {
    if (-not (Test-Path "build\windows-mingw\build.ninja")) { cmake --preset windows-mingw }
    cmake --build --preset windows-mingw
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & ".\build\windows-mingw\handwave.exe"
} finally {
    Pop-Location
}
