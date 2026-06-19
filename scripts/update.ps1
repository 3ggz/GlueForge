# One-command update: pull the latest GlueForge, rebuild, and install the VST3.
#   pwsh scripts/update.ps1
# (Commit or stash local changes first — this fast-forwards from origin/master.)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

Write-Host "=== Pulling latest from origin ==="
git -C $root pull --ff-only

Write-Host "`n=== Building (Release) ==="
& (Join-Path $PSScriptRoot 'build.ps1') Release

Write-Host "`n=== Installing the VST3 (UAC prompt) ==="
& (Join-Path $PSScriptRoot 'deploy.ps1')

Write-Host "`nUpdated. Rescan plug-ins in your DAW to pick up the new build."
Write-Host "(To build a shareable installer/zip instead, run scripts\package.ps1.)"
