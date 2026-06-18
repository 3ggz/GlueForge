# Run pluginval at the strictest level (10) against the built VST3.
# Downloads pluginval (latest release) on first run.
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$tools = Join-Path $root 'tools'
New-Item -ItemType Directory -Force $tools | Out-Null
$pv = Join-Path $tools 'pluginval.exe'

if (-not (Test-Path $pv)) {
    $zip = Join-Path $tools 'pluginval_Windows.zip'
    Write-Host "Downloading pluginval (latest)..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri 'https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Windows.zip' -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $tools -Force
}

$vst3 = Get-ChildItem -Path (Join-Path $root 'build') -Recurse -Filter 'GlueForge.vst3' -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $vst3) { throw "GlueForge.vst3 not found under build/. Build first." }

Write-Host "Validating $($vst3.FullName) at strictness 10..."
& $pv --strictness-level 10 --timeout-ms 600000 --validate "$($vst3.FullName)"
$code = $LASTEXITCODE
if ($code -ne 0) { throw "pluginval FAILED (exit $code)." }
Write-Host "`npluginval PASSED (strictness 10)."
