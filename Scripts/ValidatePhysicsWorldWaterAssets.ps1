[CmdletBinding()]
param(
    [string]$EngineRoot = "D:\unreal\UE_5.8"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$runner = Join-Path $PSScriptRoot "RunUnrealPython.ps1"
$script = Join-Path $PSScriptRoot "validate_physics_world_water_assets.py"
& $runner -ScriptPath $script -EngineRoot $EngineRoot
if ($LASTEXITCODE -ne 0) {
    throw "Physics World water asset validation failed with exit code $LASTEXITCODE."
}
