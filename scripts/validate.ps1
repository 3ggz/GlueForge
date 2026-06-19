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

$logOut = Join-Path $tools 'pluginval-stdout.log'
$logErr = Join-Path $tools 'pluginval-stderr.log'

Write-Host "Validating $($vst3.FullName) at strictness 10..."
# pluginval detaches from the console, so `&` loses its output and exit code.
# Start-Process -Wait -PassThru with redirected output captures both reliably.
$p = Start-Process -FilePath $pv `
    -ArgumentList '--strictness-level','10','--validate-in-process','--timeout-ms','600000','--validate',"`"$($vst3.FullName)`"" `
    -NoNewWindow -Wait -PassThru -RedirectStandardOutput $logOut -RedirectStandardError $logErr

Get-Content $logOut
if ($p.ExitCode -ne 0) { throw "pluginval FAILED (exit $($p.ExitCode)). See $logOut" }
Write-Host "`npluginval PASSED (strictness 10)."
