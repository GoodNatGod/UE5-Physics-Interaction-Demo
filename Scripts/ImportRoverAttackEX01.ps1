[CmdletBinding()]
param(
    [string]$AttackEX01Fbx = "",
    [string]$AssetLibraryRoot = "D:\BaiduNetdiskDownload",
    [string]$EngineRoot = "D:\unreal\UE_5.8"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$projectFiles = @(Get-ChildItem -LiteralPath $projectRoot -File -Filter "*.uproject")
if ($projectFiles.Count -ne 1) {
    throw "Expected exactly one .uproject in '$projectRoot'; found $($projectFiles.Count)."
}

$assetLibraryRoot = [IO.Path]::GetFullPath($AssetLibraryRoot)
if ([string]::IsNullOrWhiteSpace($AttackEX01Fbx)) {
    $candidates = @(
        Get-ChildItem `
            -LiteralPath $assetLibraryRoot `
            -Filter "AM_Attack_EX01.fbx" `
            -File `
            -Recurse `
            -ErrorAction Stop
    )
    if ($candidates.Count -ne 1) {
        throw "Expected exactly one AM_Attack_EX01.fbx below '$assetLibraryRoot'; found $($candidates.Count)."
    }
    $attackEX01Fbx = $candidates[0].FullName
}
else {
    $attackEX01Fbx = [IO.Path]::GetFullPath($AttackEX01Fbx)
}
if (-not (Test-Path -LiteralPath $attackEX01Fbx -PathType Leaf)) {
    throw "Attack EX01 FBX was not found: '$attackEX01Fbx'."
}
if (Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue) {
    throw "Close Unreal Editor before importing Attack EX01."
}

$editorCommand = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (-not (Test-Path -LiteralPath $editorCommand -PathType Leaf)) {
    throw "Unreal Editor commandlet was not found at '$editorCommand'."
}

$localDataCache = Join-Path $projectRoot "DerivedDataCache\Commandlet"
New-Item -ItemType Directory -Force -Path $localDataCache | Out-Null
$previousSource = $env:ROVER_ATTACK_EX01_FBX
try {
    $env:ROVER_ATTACK_EX01_FBX = $attackEX01Fbx
    & $editorCommand `
        $projectFiles[0].FullName `
        -run=pythonscript `
        "-script=$PSScriptRoot\import_rover_attack_ex01.py" `
        -unattended `
        -nop4 `
        -nosplash `
        -NoSound `
        "-LocalDataCachePath=$localDataCache"
    if ($LASTEXITCODE -ne 0) {
        throw "Attack EX01 import failed with exit code $LASTEXITCODE."
    }
}
finally {
    $env:ROVER_ATTACK_EX01_FBX = $previousSource
}

Write-Host "Attack EX01 sequence, Heavy Resonance Montage, Attack03 ResonanceWindow, and combat config were generated successfully."
