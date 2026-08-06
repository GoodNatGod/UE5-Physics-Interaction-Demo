[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EngineRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$runner = Join-Path $PSScriptRoot "RunUnrealPython.ps1"
$scriptPath = Join-Path $PSScriptRoot "disable_rover_camera_auto_follow.py"

& $runner -ScriptPath $scriptPath -EngineRoot $EngineRoot
