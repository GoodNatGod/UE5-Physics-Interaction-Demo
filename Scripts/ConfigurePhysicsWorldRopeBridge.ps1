[CmdletBinding()]
param(
    [string]$EngineRoot = "D:\unreal\UE_5.8",
    [switch]$ApplyLargePreset,
    [switch]$ApplyLongBridgeTuning,
    [switch]$SyncSharedConfigFromDemo,
    [string]$SourceMapPath,
    [int]$PlankCount
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptPath = Join-Path $PSScriptRoot "configure_physics_world_rope_bridge.py"
$presetEnvironmentName = "ROVER_ROPE_BRIDGE_APPLY_LARGE_PRESET"
$longTuningEnvironmentName = "ROVER_ROPE_BRIDGE_APPLY_LONG_TUNING"
$syncSharedConfigEnvironmentName = "ROVER_ROPE_BRIDGE_SYNC_SHARED_CONFIG"
$sourceMapEnvironmentName = "ROVER_ROPE_BRIDGE_SOURCE_MAP"
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
$previousSyncSharedConfigEnvironment = [Environment]::GetEnvironmentVariable(
    $syncSharedConfigEnvironmentName,
    [EnvironmentVariableTarget]::Process
)
$previousSourceMapEnvironment = [Environment]::GetEnvironmentVariable(
    $sourceMapEnvironmentName,
    [EnvironmentVariableTarget]::Process
)
try {
    if ($PSBoundParameters.ContainsKey("SourceMapPath") -and -not $SyncSharedConfigFromDemo) {
        throw "SourceMapPath is only valid with SyncSharedConfigFromDemo."
    }
    if ($SyncSharedConfigFromDemo -and -not $PSBoundParameters.ContainsKey("SourceMapPath")) {
        throw "SyncSharedConfigFromDemo requires an explicit SourceMapPath so OverrideSettings cannot be copied from the wrong map."
    }
    if ($SyncSharedConfigFromDemo -and (
        $ApplyLargePreset -or
        $ApplyLongBridgeTuning -or
        $PSBoundParameters.ContainsKey("PlankCount")
    )) {
        throw "SyncSharedConfigFromDemo cannot be combined with a preset or PlankCount; the source OverrideSettings must be copied without further tuning."
    }

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

    if ($SyncSharedConfigFromDemo) {
        [Environment]::SetEnvironmentVariable(
            $syncSharedConfigEnvironmentName,
            "1",
            [EnvironmentVariableTarget]::Process
        )
    }
    else {
        [Environment]::SetEnvironmentVariable(
            $syncSharedConfigEnvironmentName,
            $null,
            [EnvironmentVariableTarget]::Process
        )
    }

    if ($PSBoundParameters.ContainsKey("SourceMapPath")) {
        if ([string]::IsNullOrWhiteSpace($SourceMapPath) -or -not $SourceMapPath.StartsWith("/Game/")) {
            throw "SourceMapPath must be a non-empty Unreal package path below /Game/."
        }
        [Environment]::SetEnvironmentVariable(
            $sourceMapEnvironmentName,
            $SourceMapPath,
            [EnvironmentVariableTarget]::Process
        )
    }
    else {
        [Environment]::SetEnvironmentVariable(
            $sourceMapEnvironmentName,
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
    [Environment]::SetEnvironmentVariable(
        $syncSharedConfigEnvironmentName,
        $previousSyncSharedConfigEnvironment,
        [EnvironmentVariableTarget]::Process
    )
    [Environment]::SetEnvironmentVariable(
        $sourceMapEnvironmentName,
        $previousSourceMapEnvironment,
        [EnvironmentVariableTarget]::Process
    )
}
