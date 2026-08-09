[CmdletBinding()]
param(
    [string]$EngineRoot = "D:\unreal\UE_5.8",
    [string]$MapPath = "/Game/ThirdPerson/Lvl_ThirdPerson"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($MapPath) -or -not $MapPath.StartsWith("/Game/")) {
    throw "MapPath must be a non-empty Unreal package path below /Game/."
}

$scriptPath = Join-Path $PSScriptRoot "apply_rope_bridge_stability_tuning.py"
$mapEnvironmentName = "ROVER_ROPE_BRIDGE_TUNING_MAP"
$previousMapEnvironment = [Environment]::GetEnvironmentVariable(
    $mapEnvironmentName,
    [EnvironmentVariableTarget]::Process
)
try {
    [Environment]::SetEnvironmentVariable(
        $mapEnvironmentName,
        $MapPath,
        [EnvironmentVariableTarget]::Process
    )
    & (Join-Path $PSScriptRoot "RunUnrealPython.ps1") `
        -ScriptPath $scriptPath `
        -EngineRoot $EngineRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Rope bridge stability tuning failed with exit code $LASTEXITCODE."
    }
}
finally {
    [Environment]::SetEnvironmentVariable(
        $mapEnvironmentName,
        $previousMapEnvironment,
        [EnvironmentVariableTarget]::Process
    )
}

Write-Host "Applied targeted rope-bridge recovery, attack suppression, and jump tuning."
