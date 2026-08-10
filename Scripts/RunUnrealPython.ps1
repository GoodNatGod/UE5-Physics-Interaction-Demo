[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ScriptPath,
    [string]$EngineRoot = "D:\unreal\UE_5.8"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$projectFiles = @(Get-ChildItem -LiteralPath $projectRoot -File -Filter "*.uproject")
if ($projectFiles.Count -ne 1) {
    throw "Expected exactly one .uproject in '$projectRoot'; found $($projectFiles.Count)."
}

$scriptPath = [IO.Path]::GetFullPath($ScriptPath)
if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
    throw "Unreal Python script was not found: '$scriptPath'."
}

$editorCommand = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (-not (Test-Path -LiteralPath $editorCommand -PathType Leaf)) {
    throw "Unreal Editor commandlet was not found at '$editorCommand'."
}

$localDataCache = Join-Path $projectRoot "DerivedDataCache\Commandlet"
New-Item -ItemType Directory -Force -Path $localDataCache | Out-Null

& $editorCommand $projectFiles[0].FullName -run=pythonscript "-script=$scriptPath" -unattended -nop4 -nosplash -NoSound "-LocalDataCachePath=$localDataCache"
if ($LASTEXITCODE -ne 0) {
    throw "Unreal Python commandlet failed with exit code $LASTEXITCODE."
}
