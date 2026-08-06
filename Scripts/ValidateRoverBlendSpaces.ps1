[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EngineRoot
)

$runner = Join-Path $PSScriptRoot "RunUnrealPython.ps1"
$script = Join-Path $PSScriptRoot "validate_rover_blendspaces.py"
& $runner -ScriptPath $script -EngineRoot $EngineRoot
if ($LASTEXITCODE -ne 0) {
    throw "Rover BlendSpace validation failed with exit code $LASTEXITCODE."
}
