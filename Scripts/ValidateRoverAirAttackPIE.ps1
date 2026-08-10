[CmdletBinding()]
param(
    [string]$EngineRoot = "D:\unreal\UE_5.8",
    [ValidateRange(30, 600)][int]$TimeoutSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$projectFiles = @(Get-ChildItem -LiteralPath $projectRoot -File -Filter "*.uproject")
if ($projectFiles.Count -ne 1) {
    throw "Expected exactly one .uproject in '$projectRoot'; found $($projectFiles.Count)."
}
$scriptPath = Join-Path $PSScriptRoot "validate_rover_air_attack.py"
$editorCommand = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
    throw "Air attack PIE validation script was not found: '$scriptPath'."
}
if (-not (Test-Path -LiteralPath $editorCommand -PathType Leaf)) {
    throw "Unreal Editor command was not found: '$editorCommand'."
}
if (Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue) {
    throw "Close Unreal Editor before running the air attack PIE validation."
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$validationLog = Join-Path ([IO.Path]::GetTempPath()) "RoverReplica-AirAttack-PIE-$timestamp-$PID.log"
$localDataCache = Join-Path $projectRoot "DerivedDataCache\Validation"
New-Item -ItemType Directory -Force -Path $localDataCache | Out-Null
$arguments = @(
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
    "-ABSLOG=$validationLog",
    "-LocalDataCachePath=$localDataCache"
)
$process = Start-Process -FilePath $editorCommand -ArgumentList $arguments -PassThru -WindowStyle Hidden
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    throw "Air attack PIE validation timed out; see '$validationLog'."
}
$process.WaitForExit()

if (-not (Test-Path -LiteralPath $validationLog -PathType Leaf)) {
    throw "Air attack PIE validation produced no log."
}
$failure = Select-String -LiteralPath $validationLog -SimpleMatch "ROVER_AIR_ATTACK_PIE_FAIL" | Select-Object -Last 1
if ($failure) {
    throw "Air attack PIE validation failed: $($failure.Line.Trim()); see '$validationLog'."
}
if ($process.ExitCode -ne 0) {
    throw "Unreal Editor exited with code $($process.ExitCode); see '$validationLog'."
}
$success = Select-String -LiteralPath $validationLog -SimpleMatch "ROVER_AIR_ATTACK_PIE_OK" | Select-Object -Last 1
if (-not $success) {
    throw "Air attack PIE validation produced no success marker; see '$validationLog'."
}
Write-Host $success.Line.Trim()
Write-Host "Air attack PIE validation log: $validationLog"
