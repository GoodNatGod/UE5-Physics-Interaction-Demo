[CmdletBinding()]
param(
    [string]$EngineRoot = "D:\unreal\UE_5.8",
    [string]$WaterPlaneLabel = "BP_Waterplane2"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$environmentName = "ROVER_WATER_PLANE_LABEL"
$previousValue = [Environment]::GetEnvironmentVariable(
    $environmentName,
    [EnvironmentVariableTarget]::Process
)
try {
    [Environment]::SetEnvironmentVariable(
        $environmentName,
        $WaterPlaneLabel,
        [EnvironmentVariableTarget]::Process
    )
    & (Join-Path $PSScriptRoot "RunUnrealPython.ps1") `
        -ScriptPath (Join-Path $PSScriptRoot "validate_physics_world_waterplane_demo_assets.py") `
        -EngineRoot $EngineRoot
}
finally {
    [Environment]::SetEnvironmentVariable(
        $environmentName,
        $previousValue,
        [EnvironmentVariableTarget]::Process
    )
}
