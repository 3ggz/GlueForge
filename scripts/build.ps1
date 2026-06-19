# Configure + build GlueForge (VST3, Standalone, Tests) with MSVC + Ninja.
#
# Version-agnostic: locates whatever VS C++ toolset is installed (2022 / v18 / ...)
# via vswhere, sources vcvars64, and drives CMake's Ninja generator. This avoids
# depending on a specific "Visual Studio NN" generator name.
#
# Usage:  pwsh scripts/build.ps1 [Release|Debug|RelWithDebInfo]
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$config = if ($args.Count -ge 1) { $args[0] } else { 'Release' }

function Resolve-CMake {
    $c = Get-Command cmake -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    foreach ($p in @(
        "$env:ProgramFiles\CMake\bin\cmake.exe",
        "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe")) {
        if (Test-Path $p) { return $p }
    }
    throw "cmake not found."
}

function Resolve-Ninja {
    $c = Get-Command ninja -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    $w = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter ninja.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($w) { return $w.FullName }
    throw "ninja not found."
}

function Resolve-VSPath {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere not found (is VS Build Tools installed?)." }
    $p = & $vswhere -products * -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $p) { throw "No VS install with the C++ toolset (VC.Tools.x86.x64) found." }
    return ($p | Select-Object -First 1).Trim()
}

$cmake    = Resolve-CMake
$ninja    = Resolve-Ninja
$vs       = Resolve-VSPath
$vcvars   = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$build    = Join-Path $root 'build'
$ninjaFwd = $ninja -replace '\\', '/'

Write-Host "cmake:   $cmake"
Write-Host "ninja:   $ninja"
Write-Host "vs:      $vs"
Write-Host "config:  $config`n"

# Run inside an MSVC environment by sourcing vcvars64 in a child cmd.
$bat = @"
@echo off
call "$vcvars" >nul || exit /b 1
"$cmake" -S "$root" -B "$build" -G Ninja -DCMAKE_MAKE_PROGRAM="$ninjaFwd" -DCMAKE_BUILD_TYPE=$config || exit /b 1
"$cmake" --build "$build" --parallel || exit /b 1
"@
$batFile = Join-Path $env:TEMP 'gf_build.bat'
Set-Content -LiteralPath $batFile -Value $bat -Encoding Ascii

& cmd /c "`"$batFile`""
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }

Write-Host "`nBuild complete ($config)."
