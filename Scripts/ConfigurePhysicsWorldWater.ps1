[CmdletBinding()]
param(
    [string]$EngineRoot = "D:\unreal\UE_5.8",
    [ValidateRange(30, 600)][int]$TimeoutSeconds = 240
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$projectFiles = @(Get-ChildItem -LiteralPath $projectRoot -File -Filter "*.uproject")
if ($projectFiles.Count -ne 1) {
    throw "Expected exactly one .uproject in '$projectRoot'; found $($projectFiles.Count)."
}

$scriptPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "configure_physics_world_water.py"))
$editorCommand = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (-not (Test-Path -LiteralPath $editorCommand -PathType Leaf)) {
    throw "Unreal Editor command was not found at '$editorCommand'."
}
if (Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue) {
    throw "Close Unreal Editor before configuring the water assets."
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$configurationLog = Join-Path ([IO.Path]::GetTempPath()) "PhysicsWorld-Water-Config-$timestamp-$PID.log"
$localDataCache = Join-Path $projectRoot "DerivedDataCache\Commandlet"
New-Item -ItemType Directory -Force -Path $localDataCache | Out-Null
$editorArguments = @(
    $projectFiles[0].FullName,
    "-ExecutePythonScript=$scriptPath",
    "-ScriptErrorsAreFatal",
    "-unattended",
    "-nop4",
    "-nosplash",
    "-NoSound",
    "-NoLiveCoding",
    "-stdout",
    "-FullStdOutLogOutput",
    "-ABSLOG=$configurationLog",
    "-LocalDataCachePath=$localDataCache"
)

$editorProcess = Start-Process `
    -FilePath $editorCommand `
    -ArgumentList $editorArguments `
    -PassThru `
    -WindowStyle Hidden
if (-not $editorProcess.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $editorProcess.Id -Force -ErrorAction SilentlyContinue
    throw "Water asset configuration timed out after $TimeoutSeconds seconds; see '$configurationLog'."
}
$editorProcess.WaitForExit()
if ($editorProcess.ExitCode -ne 0) {
    throw "Water asset configuration failed with exit code $($editorProcess.ExitCode); see '$configurationLog'."
}

$success = Select-String -LiteralPath $configurationLog -SimpleMatch "PHYSICS_WORLD_WATER_CONFIG_OK" | Select-Object -Last 1
if (-not $success) {
    throw "Water asset configuration produced no success marker; see '$configurationLog'."
}
Write-Host $success.Line.Trim()
Write-Host "Water asset configuration log: $configurationLog"
