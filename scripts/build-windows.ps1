[CmdletBinding()]
param(
    [string]$Preset = "full-release",
    [int]$Jobs = [Environment]::ProcessorCount,
    [switch]$NoFresh
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot

$LlvmCandidates = @()
if ($env:LLVM_HOME) {
    $LlvmCandidates += (Join-Path $env:LLVM_HOME "bin")
}
$LlvmCandidates += "C:\Program Files\LLVM\bin"
$LlvmCandidates += "C:\Program Files (x86)\LLVM\bin"

$LlvmBin = $LlvmCandidates | Where-Object {
    (Test-Path (Join-Path $_ "clang.exe")) -and
    (Test-Path (Join-Path $_ "llvm-rc.exe"))
} | Select-Object -First 1

if (-not $LlvmBin) {
    throw @"
LLVM/Clang and llvm-rc were not found.
Install them with:
  winget install --id LLVM.LLVM --exact --source winget
Then open a new PowerShell window and run this script again.
"@
}

foreach ($Command in @("cmake", "ninja", "py")) {
    if (-not (Get-Command $Command -ErrorAction SilentlyContinue)) {
        throw "Required command '$Command' was not found in PATH."
    }
}

$env:Path = "$LlvmBin;$env:Path"
$env:CC = Join-Path $LlvmBin "clang.exe"
$env:RC = Join-Path $LlvmBin "llvm-rc.exe"

Write-Host "TurboJS Windows build"
Write-Host "  Project:  $ProjectRoot"
Write-Host "  Preset:   $Preset"
Write-Host "  Jobs:     $Jobs"
Write-Host "  Compiler: $env:CC"
Write-Host "  Resource: $env:RC"

$Arguments = @(
    "-3",
    (Join-Path $ProjectRoot "scripts\build.py"),
    "--preset", $Preset,
    "--jobs", "$Jobs"
)
if (-not $NoFresh) {
    $Arguments += "--fresh"
}

Push-Location $ProjectRoot
try {
    & py @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "TurboJS build failed with exit code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}
