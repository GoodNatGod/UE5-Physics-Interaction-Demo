[CmdletBinding()]
param(
    [string]$EngineRoot = "D:\unreal\UE_5.8"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptPath = Join-Path $PSScriptRoot "configure_rover_attack_weapon_hands.py"
& (Join-Path $PSScriptRoot "RunUnrealPython.ps1") `
    -ScriptPath $scriptPath `
    -EngineRoot $EngineRoot
if ($LASTEXITCODE -ne 0) {
    throw "Attack weapon-hand configuration failed with exit code $LASTEXITCODE."
}

Write-Host "Attack01 now uses the left hand; Attack02 and Attack03 use the right hand."
