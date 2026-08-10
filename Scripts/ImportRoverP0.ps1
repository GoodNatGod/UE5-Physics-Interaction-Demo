[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$SourceRoot,
    [string]$TPosePath,
    [string]$EngineRoot = "D:\unreal\UE_5.8"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$projectFiles = @(Get-ChildItem -LiteralPath $projectRoot -File -Filter "*.uproject")
if ($projectFiles.Count -ne 1) {
    throw "Expected exactly one .uproject in '$projectRoot'; found $($projectFiles.Count)."
}

$sourceRoot = [IO.Path]::GetFullPath($SourceRoot)
if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
    throw "FBX source directory was not found: '$sourceRoot'."
}

if (-not $TPosePath) {
    $tPoseCandidates = @(
        Get-ChildItem -LiteralPath $sourceRoot -File -Filter "*.fbx" |
            Where-Object { $_.Name.StartsWith([char]0x00B7) }
    )
    if ($tPoseCandidates.Count -ne 1) {
        throw "Could not identify one T-pose FBX. Pass -TPosePath explicitly."
    }
    $TPosePath = $tPoseCandidates[0].FullName
}

$tPosePath = [IO.Path]::GetFullPath($TPosePath)
if (-not (Test-Path -LiteralPath $tPosePath -PathType Leaf)) {
    throw "T-pose FBX was not found: '$tPosePath'."
}

$editorCommand = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (-not (Test-Path -LiteralPath $editorCommand -PathType Leaf)) {
    throw "Unreal Editor commandlet was not found at '$editorCommand'."
}

$localDataCache = Join-Path $projectRoot "DerivedDataCache\Commandlet"
New-Item -ItemType Directory -Force -Path $localDataCache | Out-Null

$previousSource = $env:ROVER_FBX_SOURCE
$previousTPose = $env:ROVER_TPOSE_FBX
try {
    $env:ROVER_FBX_SOURCE = $sourceRoot
    $env:ROVER_TPOSE_FBX = $tPosePath
    & $editorCommand $projectFiles[0].FullName -run=pythonscript "-script=$PSScriptRoot\import_rover_p0.py" -unattended -nop4 -nosplash -NoSound "-LocalDataCachePath=$localDataCache"
    if ($LASTEXITCODE -ne 0) {
        throw "Rover P0 import failed with exit code $LASTEXITCODE."
    }
}
finally {
    $env:ROVER_FBX_SOURCE = $previousSource
    $env:ROVER_TPOSE_FBX = $previousTPose
}

Write-Host "Rover T-pose and P0 animations were imported successfully."
