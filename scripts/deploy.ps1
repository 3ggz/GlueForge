# Install the built VST3 into the system VST3 folder so Ableton can scan it.
# Triggers a UAC prompt (writing to Program Files needs admin).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$vst3 = Get-ChildItem -Path (Join-Path $root 'build') -Recurse -Filter 'GlueForge.vst3' -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $vst3) { throw "GlueForge.vst3 not found under build/. Build first." }

$dest = Join-Path $env:CommonProgramFiles 'VST3'
$src  = $vst3.FullName
Write-Host "Installing $src -> $dest (will prompt for admin)..."

$inner = "New-Item -ItemType Directory -Force '$dest' | Out-Null; Copy-Item -Path '$src' -Destination '$dest' -Recurse -Force; Write-Host 'Installed GlueForge.vst3 to $dest'"
Start-Process powershell -Verb RunAs -Wait -ArgumentList '-NoProfile','-NonInteractive','-Command', $inner
Write-Host "Done. Rescan plugins in Ableton (Preferences > Plug-Ins > Rescan)."
