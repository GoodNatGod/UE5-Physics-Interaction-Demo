[CmdletBinding()]
param(
    [string]$EngineRoot = "D:\unreal\UE_5.8"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "RunUnrealPython.ps1") `
    -ScriptPath (Join-Path $PSScriptRoot "restore_rover_hair_material.py") `
    -EngineRoot $EngineRoot
