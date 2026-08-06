[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EngineRoot,
    [switch]$ApplyLargePreset,
    [switch]$ApplyLongBridgeTuning,
    [int]$PlankCount
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptPath = Join-Path $PSScriptRoot "configure_physics_world_rope_bridge.py"
$presetEnvironmentName = "ROVER_ROPE_BRIDGE_APPLY_LARGE_PRESET"
$longTuningEnvironmentName = "ROVER_ROPE_BRIDGE_APPLY_LONG_TUNING"
$plankCountEnvironmentName = "ROVER_ROPE_BRIDGE_PLANK_COUNT"
$previousPresetEnvironment = [Environment]::GetEnvironmentVariable(
    $presetEnvironmentName,
    [EnvironmentVariableTarget]::Process
)
$previousPlankCountEnvironment = [Environment]::GetEnvironmentVariable(
    $plankCountEnvironmentName,
    [EnvironmentVariableTarget]::Process
)
$previousLongTuningEnvironment = [Environment]::GetEnvironmentVariable(
    $longTuningEnvironmentName,
    [EnvironmentVariableTarget]::Process
)
try {
    if ($ApplyLargePreset) {
        [Environment]::SetEnvironmentVariable(
            $presetEnvironmentName,
            "1",
            [EnvironmentVariableTarget]::Process
        )
    }
    else {
        [Environment]::SetEnvironmentVariable(
            $presetEnvironmentName,
            $null,
            [EnvironmentVariableTarget]::Process
        )
    }

    if ($ApplyLongBridgeTuning) {
        [Environment]::SetEnvironmentVariable(
            $longTuningEnvironmentName,
            "1",
            [EnvironmentVariableTarget]::Process
        )
    }
    else {
        [Environment]::SetEnvironmentVariable(
            $longTuningEnvironmentName,
            $null,
            [EnvironmentVariableTarget]::Process
        )
    }

    if ($PSBoundParameters.ContainsKey("PlankCount")) {
        if ($PlankCount -lt 12) {
            throw "PlankCount must be at least 12; got $PlankCount."
        }
        [Environment]::SetEnvironmentVariable(
            $plankCountEnvironmentName,
            $PlankCount.ToString([Globalization.CultureInfo]::InvariantCulture),
            [EnvironmentVariableTarget]::Process
        )
    }
    else {
        [Environment]::SetEnvironmentVariable(
            $plankCountEnvironmentName,
            $null,
            [EnvironmentVariableTarget]::Process
        )
    }

    & (Join-Path $PSScriptRoot "RunUnrealPython.ps1") `
        -ScriptPath $scriptPath `
        -EngineRoot $EngineRoot
}
finally {
    [Environment]::SetEnvironmentVariable(
        $presetEnvironmentName,
        $previousPresetEnvironment,
        [EnvironmentVariableTarget]::Process
    )
    [Environment]::SetEnvironmentVariable(
        $plankCountEnvironmentName,
        $previousPlankCountEnvironment,
        [EnvironmentVariableTarget]::Process
    )
    [Environment]::SetEnvironmentVariable(
        $longTuningEnvironmentName,
        $previousLongTuningEnvironment,
        [EnvironmentVariableTarget]::Process
    )
}
