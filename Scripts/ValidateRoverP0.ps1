[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EngineRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$projectFiles = @(Get-ChildItem -LiteralPath $projectRoot -File -Filter "*.uproject")
if ($projectFiles.Count -ne 1) {
    throw "Expected exactly one .uproject in '$projectRoot'; found $($projectFiles.Count)."
}

$editorCommand = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (-not (Test-Path -LiteralPath $editorCommand -PathType Leaf)) {
    throw "Unreal Editor commandlet was not found at '$editorCommand'."
}

$localDataCache = Join-Path $projectRoot "DerivedDataCache\Commandlet"
New-Item -ItemType Directory -Force -Path $localDataCache | Out-Null

& $editorCommand $projectFiles[0].FullName -run=pythonscript "-script=$PSScriptRoot\validate_rover_p0.py" -unattended -nop4 -nosplash -NoSound "-LocalDataCachePath=$localDataCache"
if ($LASTEXITCODE -ne 0) {
    throw "Rover P0 validation failed with exit code $LASTEXITCODE."
}

Write-Host "Rover P0 assets passed validation."
