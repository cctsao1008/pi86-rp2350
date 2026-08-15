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

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$SdkPath = Join-Path $RepoRoot "third_party/pico-sdk"
$SdkInit = Join-Path $SdkPath "pico_sdk_init.cmake"
$ResolvedBuildDir = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir
} else {
    Join-Path $RepoRoot $BuildDir
}

if (-not (Test-Path $SdkInit)) {
    throw @"
Pico SDK submodule is missing or incomplete:
  $SdkPath

Run from the repository root:
  git submodule update --init --recursive
"@
}

if ($Clean -and (Test-Path $ResolvedBuildDir)) {
    Write-Host "Removing $ResolvedBuildDir"
    Remove-Item -Recurse -Force $ResolvedBuildDir
}

Write-Host "Repository    = $RepoRoot"
Write-Host "Pico SDK      = $SdkPath"
Write-Host "PICO_BOARD    = waveshare_rp2350_pizero"
Write-Host "BuildDir      = $ResolvedBuildDir"

Push-Location $RepoRoot
try {
    Invoke-Checked cmake `
        -S . `
        -B $ResolvedBuildDir `
        -DPICO_BOARD=waveshare_rp2350_pizero `
        -DCMAKE_BUILD_TYPE=Release

    $buildArgs = @("--build", $ResolvedBuildDir, "--parallel")
    if ($Target) {
        $buildArgs += @("--target", $Target)
    }

    Invoke-Checked cmake @buildArgs
}
finally {
    Pop-Location
}

Write-Host ""
Write-Host "Generated UF2 files:"
Get-ChildItem -Path $ResolvedBuildDir -Filter *.uf2 -Recurse -ErrorAction SilentlyContinue |
    ForEach-Object { Write-Host "  $($_.FullName)" }
