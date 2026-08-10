[CmdletBinding()]
param(
    [string]$EngineRoot = "D:\unreal\UE_5.8",
    [ValidateRange(0.05, 1.0)][float]$BlendOutTime = 0.25
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue) {
    throw "Close Unreal Editor before updating the Heavy Resonance recovery blend."
}

$scriptPath = Join-Path $PSScriptRoot "configure_rover_heavy_resonance_recovery.py"
$previousBlendOut = $env:ROVER_RESONANCE_BLEND_OUT_TIME
try {
    $env:ROVER_RESONANCE_BLEND_OUT_TIME = $BlendOutTime.ToString(
        [Globalization.CultureInfo]::InvariantCulture
    )
    & (Join-Path $PSScriptRoot "RunUnrealPython.ps1") `
        -ScriptPath $scriptPath `
        -EngineRoot $EngineRoot
}
finally {
    $env:ROVER_RESONANCE_BLEND_OUT_TIME = $previousBlendOut
}
