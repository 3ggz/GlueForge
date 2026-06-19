# Build a release and produce a Windows distributable:
#   - dist/GlueForge-Windows-<version>.zip  (VST3 + Standalone + README)
#   - dist/GlueForge-Setup-<version>.exe     (if Inno Setup's ISCC is installed)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$version = '0.1.0'

Write-Host "=== Building Release ==="
& (Join-Path $PSScriptRoot 'build.ps1') Release

$vst3 = (Get-ChildItem "$root\build" -Recurse -Filter 'GlueForge.vst3' -Directory | Select-Object -First 1).FullName
$exe  = (Get-ChildItem "$root\build" -Recurse -Filter 'GlueForge.exe' -File |
            Where-Object { $_.FullName -match 'Standalone' } | Select-Object -First 1).FullName
if (-not $vst3 -or -not $exe) { throw "Build artifacts not found." }

$stage = Join-Path $root "dist\GlueForge-$version"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$stage\VST3" | Out-Null
New-Item -ItemType Directory -Force "$stage\Standalone" | Out-Null
Copy-Item $vst3 "$stage\VST3" -Recurse -Force
Copy-Item $exe  "$stage\Standalone" -Force
Copy-Item "$root\README.md" $stage -Force

$zip = Join-Path $root "dist\GlueForge-Windows-$version.zip"
Remove-Item $zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Write-Host "`nPackaged ZIP: $zip"

# Optional installer via Inno Setup
$iscc = (Get-Command iscc -ErrorAction SilentlyContinue).Source
if (-not $iscc)
{
    foreach ($p in @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe"))
        { if (Test-Path $p) { $iscc = $p; break } }
}
if ($iscc)
{
    & $iscc "$root\installer\GlueForge.iss" "/DMyVersion=$version"
    Write-Host "Installer built in dist\."
}
else
{
    Write-Host "Inno Setup (ISCC) not found - ZIP produced. Install Inno Setup 6 to build"
    Write-Host "the installer from installer\GlueForge.iss (or just unzip and copy the .vst3)."
}
