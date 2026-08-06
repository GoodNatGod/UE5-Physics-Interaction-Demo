[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EngineRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptPath = Join-Path $PSScriptRoot "configure_rover_animation_assets.py"
if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
    throw "Rover animation configuration script was not found: '$scriptPath'."
}

& (Join-Path $PSScriptRoot "RunUnrealPython.ps1") `
    -ScriptPath $scriptPath `
    -EngineRoot $EngineRoot

Write-Host "Rover P0 animations were configured for in-place playback."
