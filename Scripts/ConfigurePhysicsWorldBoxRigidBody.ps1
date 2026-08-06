[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EngineRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptPath = Join-Path $PSScriptRoot "configure_physics_world_box_rigidbody.py"
& (Join-Path $PSScriptRoot "RunUnrealPython.ps1") `
    -ScriptPath $scriptPath `
    -EngineRoot $EngineRoot
if ($LASTEXITCODE -ne 0) {
    throw "Physics World box rigid-body configuration failed with exit code $LASTEXITCODE."
}

Write-Host "Physics World boxes now simulate physics with per-instance mass and shared world gravity."
