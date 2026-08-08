[CmdletBinding()]
param(
    [string]$Attack05Fbx = "",
    [string]$AssetLibraryRoot = "",
    [string]$EngineRoot = "D:\unreal\UE_5.8"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$projectFiles = @(Get-ChildItem -LiteralPath $projectRoot -File -Filter "*.uproject")
if ($projectFiles.Count -ne 1) {
    throw "Expected exactly one .uproject in '$projectRoot'; found $($projectFiles.Count)."
}

if ([string]::IsNullOrWhiteSpace($Attack05Fbx)) {
    if ([string]::IsNullOrWhiteSpace($AssetLibraryRoot)) {
        throw "Pass -Attack05Fbx or -AssetLibraryRoot with licensed source assets."
    }
    $assetLibraryRoot = [IO.Path]::GetFullPath($AssetLibraryRoot)
    $attack05Candidates = @(
        Get-ChildItem `
            -LiteralPath $assetLibraryRoot `
            -Filter "AM_Attack05.fbx" `
            -File `
            -Recurse `
            -ErrorAction Stop
    )
    if ($attack05Candidates.Count -ne 1) {
        throw "Expected exactly one AM_Attack05.fbx below '$assetLibraryRoot'; found $($attack05Candidates.Count)."
    }
    $attack05Fbx = $attack05Candidates[0].FullName
}
else {
    $attack05Fbx = [IO.Path]::GetFullPath($Attack05Fbx)
}
if (-not (Test-Path -LiteralPath $attack05Fbx -PathType Leaf)) {
    throw "Attack05 FBX was not found: '$attack05Fbx'."
}
if (Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue) {
    throw "Close Unreal Editor before importing Attack05."
}

$editorCommand = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (-not (Test-Path -LiteralPath $editorCommand -PathType Leaf)) {
    throw "Unreal Editor commandlet was not found at '$editorCommand'."
}

$localDataCache = Join-Path $projectRoot "DerivedDataCache\Commandlet"
New-Item -ItemType Directory -Force -Path $localDataCache | Out-Null
$previousAttack05Fbx = $env:ROVER_ATTACK05_FBX
try {
    $env:ROVER_ATTACK05_FBX = $attack05Fbx
    & $editorCommand `
        $projectFiles[0].FullName `
        -run=pythonscript `
        "-script=$PSScriptRoot\import_rover_attack05.py" `
        -unattended `
        -nop4 `
        -nosplash `
        -NoSound `
        "-LocalDataCachePath=$localDataCache"
    if ($LASTEXITCODE -ne 0) {
        throw "Attack05 import failed with exit code $LASTEXITCODE."
    }
}
finally {
    $env:ROVER_ATTACK05_FBX = $previousAttack05Fbx
}

Write-Host "Attack05 sequence, heavy Montage, Notify timeline, and combat config were generated successfully."
