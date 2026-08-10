[CmdletBinding()]
param(
    [string]$EngineRoot = "D:\unreal\UE_5.8"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "RunUnrealPython.ps1") `
    -ScriptPath (Join-Path $PSScriptRoot "configure_physics_world_dual_lake_demo.py") `
    -EngineRoot $EngineRoot
