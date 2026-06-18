# Configure + build GlueForge (VST3, Standalone, Tests).
# Usage:  pwsh scripts/build.ps1 [Release|Debug]
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Resolve-CMake {
    $c = Get-Command cmake -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    $candidates = @(
        "$env:ProgramFiles\CMake\bin\cmake.exe",
        "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )
    foreach ($p in $candidates) { if (Test-Path $p) { return $p } }
    throw "cmake not found on PATH or in standard locations."
}

$cmake  = Resolve-CMake
$build  = Join-Path $root 'build'
$config = if ($args.Count -ge 1) { $args[0] } else { 'Release' }

Write-Host "cmake:  $cmake"
Write-Host "config: $config"

& $cmake -S $root -B $build -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)." }

& $cmake --build $build --config $config --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }

Write-Host "`nBuild complete ($config)."
