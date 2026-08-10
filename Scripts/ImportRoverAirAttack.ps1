[CmdletBinding()]
param(
    [string]$SourceRoot = "D:\BaiduNetdiskDownload",
    [string]$EngineRoot = "D:\unreal\UE_5.8"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$projectFiles = @(Get-ChildItem -LiteralPath $projectRoot -File -Filter "*.uproject")
if ($projectFiles.Count -ne 1) {
    throw "Expected exactly one .uproject in '$projectRoot'; found $($projectFiles.Count)."
}

$resolvedSourceRoot = [IO.Path]::GetFullPath($SourceRoot)
$sources = @{}
foreach ($phase in @("Start", "Loop", "End")) {
    $candidates = @(
        Get-ChildItem `
            -LiteralPath $resolvedSourceRoot `
            -Filter "AM_AirAttack_$phase.fbx" `
            -File `
            -Recurse `
            -ErrorAction Stop
    )
    if ($candidates.Count -ne 1) {
        throw "Expected exactly one AM_AirAttack_$phase.fbx below '$resolvedSourceRoot'; found $($candidates.Count)."
    }
    $sources[$phase] = $candidates[0].FullName
}

if (Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue) {
    throw "Close Unreal Editor before importing AirAttack assets."
}

$editorCommand = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (-not (Test-Path -LiteralPath $editorCommand -PathType Leaf)) {
    throw "Unreal Editor commandlet was not found at '$editorCommand'."
}

$localDataCache = Join-Path $projectRoot "DerivedDataCache\Commandlet"
New-Item -ItemType Directory -Force -Path $localDataCache | Out-Null
$previousStart = $env:ROVER_AIR_ATTACK_START_FBX
$previousLoop = $env:ROVER_AIR_ATTACK_LOOP_FBX
$previousEnd = $env:ROVER_AIR_ATTACK_END_FBX
try {
    $env:ROVER_AIR_ATTACK_START_FBX = $sources["Start"]
    $env:ROVER_AIR_ATTACK_LOOP_FBX = $sources["Loop"]
    $env:ROVER_AIR_ATTACK_END_FBX = $sources["End"]
    & $editorCommand `
        $projectFiles[0].FullName `
        -run=pythonscript `
        "-script=$PSScriptRoot\import_rover_air_attack.py" `
        -unattended `
        -nop4 `
        -nosplash `
        -NoSound `
        "-LocalDataCachePath=$localDataCache"
    if ($LASTEXITCODE -ne 0) {
        throw "AirAttack import failed with exit code $LASTEXITCODE."
    }
}
finally {
    $env:ROVER_AIR_ATTACK_START_FBX = $previousStart
    $env:ROVER_AIR_ATTACK_LOOP_FBX = $previousLoop
    $env:ROVER_AIR_ATTACK_END_FBX = $previousEnd
}

Write-Host "AirAttack Start, Loop, End sequences and the sectioned Montage were generated successfully."
