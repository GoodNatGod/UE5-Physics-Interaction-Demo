[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EngineRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptPath = Join-Path $PSScriptRoot "configure_physics_world_p0.py"
& (Join-Path $PSScriptRoot "RunUnrealPython.ps1") `
    -ScriptPath $scriptPath `
    -EngineRoot $EngineRoot
