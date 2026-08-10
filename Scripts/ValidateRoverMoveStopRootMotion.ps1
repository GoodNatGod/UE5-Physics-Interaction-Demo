[CmdletBinding()]
param(
    [string]$EngineRoot = "D:\unreal\UE_5.8"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptPath = Join-Path $PSScriptRoot "validate_rover_move_stop_root_motion.py"
& (Join-Path $PSScriptRoot "RunUnrealPython.ps1") -ScriptPath $scriptPath -EngineRoot $EngineRoot
if ($LASTEXITCODE -ne 0) {
    throw "Move-stop root-motion validation failed with exit code $LASTEXITCODE."
}
