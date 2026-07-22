$ErrorActionPreference = 'Stop'

$projectDir = $PSScriptRoot
$localConfig = Join-Path $projectDir 'platformio.local.ini'
$source = Join-Path $projectDir '.pio\build\esp8266\firmware.bin'
$destination = Join-Path $projectDir 'firmware.bin'

if (-not (Test-Path -LiteralPath $localConfig)) {
    throw 'Missing platformio.local.ini. Copy platformio.local.example.ini and fill in your settings.'
}

if (Get-Command pio -ErrorAction SilentlyContinue) {
    & pio run --project-dir $projectDir --environment esp8266
} elseif (Get-Command py -ErrorAction SilentlyContinue) {
    & py -3.12 -m platformio run --project-dir $projectDir --environment esp8266
} else {
    throw 'PlatformIO was not found. Install it with: py -3.12 -m pip install platformio'
}

if ($LASTEXITCODE -ne 0) {
    throw "PlatformIO build failed with exit code $LASTEXITCODE."
}

if (-not (Test-Path -LiteralPath $source)) {
    throw "Build succeeded but firmware was not found at: $source"
}

Copy-Item -LiteralPath $source -Destination $destination -Force
Write-Host "Created $destination"
