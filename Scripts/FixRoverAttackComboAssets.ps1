[CmdletBinding()]
param(
    [string]$EngineRoot = "D:\unreal\UE_5.8"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptPath = Join-Path $PSScriptRoot "fix_rover_attack_combo_assets.py"
& (Join-Path $PSScriptRoot "RunUnrealPython.ps1") `
    -ScriptPath $scriptPath `
    -EngineRoot $EngineRoot
if ($LASTEXITCODE -ne 0) {
    throw "Attack combo asset repair failed with exit code $LASTEXITCODE."
}

Write-Host "Attack01-03 Root Lock, Notify, Montage blend, and combat config assets were repaired."
