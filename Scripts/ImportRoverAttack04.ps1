[CmdletBinding()]
param(
    [string]$Attack04Fbx = "",
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

if ([string]::IsNullOrWhiteSpace($Attack04Fbx)) {
    if ([string]::IsNullOrWhiteSpace($AssetLibraryRoot)) {
        throw "Pass -Attack04Fbx or -AssetLibraryRoot with licensed source assets."
    }
    $assetLibraryRoot = [IO.Path]::GetFullPath($AssetLibraryRoot)
    $attack04Candidates = @(
        Get-ChildItem `
            -LiteralPath $assetLibraryRoot `
            -Filter "AM_Attack04.fbx" `
            -File `
            -Recurse `
            -ErrorAction Stop
    )
    if ($attack04Candidates.Count -ne 1) {
        throw "Expected exactly one AM_Attack04.fbx below '$assetLibraryRoot'; found $($attack04Candidates.Count)."
    }
    $attack04Fbx = $attack04Candidates[0].FullName
}
else {
    $attack04Fbx = [IO.Path]::GetFullPath($Attack04Fbx)
}
if (-not (Test-Path -LiteralPath $attack04Fbx -PathType Leaf)) {
    throw "Attack04 FBX was not found: '$attack04Fbx'."
}
if (Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue) {
    throw "Close Unreal Editor before importing Attack04."
}

$editorCommand = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (-not (Test-Path -LiteralPath $editorCommand -PathType Leaf)) {
    throw "Unreal Editor commandlet was not found at '$editorCommand'."
}

$localDataCache = Join-Path $projectRoot "DerivedDataCache\Commandlet"
New-Item -ItemType Directory -Force -Path $localDataCache | Out-Null
$previousAttack04Fbx = $env:ROVER_ATTACK04_FBX
try {
    $env:ROVER_ATTACK04_FBX = $attack04Fbx
    & $editorCommand `
        $projectFiles[0].FullName `
        -run=pythonscript `
        "-script=$PSScriptRoot\import_rover_attack04.py" `
        -unattended `
        -nop4 `
        -nosplash `
        -NoSound `
        "-LocalDataCachePath=$localDataCache"
    if ($LASTEXITCODE -ne 0) {
        throw "Attack04 import failed with exit code $LASTEXITCODE."
    }
}
finally {
    $env:ROVER_ATTACK04_FBX = $previousAttack04Fbx
}

Write-Host "Attack04 sequence, Montage, Notify timeline, and combat config were generated successfully."
