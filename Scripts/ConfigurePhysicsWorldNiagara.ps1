[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EngineRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptPath = Join-Path $PSScriptRoot "configure_physics_world_niagara.py"
& (Join-Path $PSScriptRoot "RunUnrealPython.ps1") `
    -ScriptPath $scriptPath `
    -EngineRoot $EngineRoot
if ($LASTEXITCODE -ne 0) {
    throw "Physics World Niagara configuration failed with exit code $LASTEXITCODE."
}

Write-Host "Physics World Niagara systems generated and assigned to the shared config."
