# Render the GlueForge user manual (HTML -> PDF) using headless Chrome or Edge.
#   pwsh scripts/build-manual.ps1
# Output: Resources/GlueForgeManual.pdf  (committed; embedded into the plugin at build time)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$html = Join-Path $root 'Resources\manual\GlueForge-Manual.html'
$pdf  = Join-Path $root 'Resources\GlueForgeManual.pdf'

if (-not (Test-Path $html)) { throw "Manual HTML not found: $html" }

$browser = @(
    "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $browser) { throw "No Chrome or Edge found to render the PDF." }

# file:// URL needs forward slashes.
$uri = 'file:///' + ($html -replace '\\','/')
$tmpProfile = Join-Path $env:TEMP ("gf-manual-" + [System.Guid]::NewGuid().ToString('N'))

Write-Host "Browser: $browser"
Write-Host "Render:  $uri"
Write-Host "Output:  $pdf"

$args = @(
    '--headless=new',
    '--disable-gpu',
    '--no-sandbox',
    '--no-pdf-header-footer',         # new flag name
    '--print-to-pdf-no-header',       # older flag name (ignored if unknown)
    "--user-data-dir=$tmpProfile",
    "--print-to-pdf=$pdf",
    '--no-margins',
    $uri
)

Remove-Item $pdf -Force -ErrorAction SilentlyContinue
$p = Start-Process -FilePath $browser -ArgumentList $args -Wait -PassThru -NoNewWindow
Remove-Item $tmpProfile -Recurse -Force -ErrorAction SilentlyContinue

if (-not (Test-Path $pdf)) { throw "PDF was not produced (browser exit $($p.ExitCode))." }
$kb = [math]::Round((Get-Item $pdf).Length / 1KB, 1)
Write-Host "Manual rendered: $pdf  ($kb KB)"
