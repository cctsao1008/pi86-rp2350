param(
    [string]$BuildDir = "build",
    [string]$Target = "",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Command,

        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Arguments
    )

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command failed with exit code $LASTEXITCODE"
    }
}

if (-not $env:PICO_SDK_PATH) {
    throw "PICO_SDK_PATH is not set. Point it to a Raspberry Pi Pico SDK 2.3.0+ checkout."
}

$sdkImport = Join-Path $env:PICO_SDK_PATH "external/pico_sdk_import.cmake"
if (-not (Test-Path $sdkImport)) {
    throw "PICO_SDK_PATH does not look valid: $env:PICO_SDK_PATH"
}

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Removing $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

Write-Host "PICO_SDK_PATH = $env:PICO_SDK_PATH"
Write-Host "PICO_BOARD    = waveshare_rp2350_pizero"
Write-Host "BuildDir      = $BuildDir"

Invoke-Checked cmake `
    -S . `
    -B $BuildDir `
    -DPICO_BOARD=waveshare_rp2350_pizero

$buildArgs = @("--build", $BuildDir, "--parallel")
if ($Target) {
    $buildArgs += @("--target", $Target)
}

Invoke-Checked cmake @buildArgs

Write-Host ""
Write-Host "Generated UF2 files:"
Get-ChildItem -Path $BuildDir -Filter *.uf2 -Recurse -ErrorAction SilentlyContinue |
    ForEach-Object { Write-Host "  $($_.FullName)" }
