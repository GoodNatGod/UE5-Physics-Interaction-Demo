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

$scriptPath = Join-Path $PSScriptRoot "validate_physics_world_p0.py"
if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
    throw "Physics World P0 PIE script was not found: '$scriptPath'."
}

$editorCommand = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (-not (Test-Path -LiteralPath $editorCommand -PathType Leaf)) {
    throw "Unreal Editor command was not found at '$editorCommand'."
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$validationLog = Join-Path ([IO.Path]::GetTempPath()) "PhysicsWorldP0-PIE-$timestamp-$PID.log"
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
    "-ABSLOG=$validationLog"
)

$editorProcess = Start-Process `
    -FilePath $editorCommand `
    -ArgumentList $editorArguments `
    -PassThru `
    -WindowStyle Hidden

if (-not $editorProcess.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $editorProcess.Id -Force -ErrorAction SilentlyContinue
    throw "Physics World P0 PIE validation timed out after $TimeoutSeconds seconds; see '$validationLog'."
}
$editorProcess.WaitForExit()
$exitCode = $editorProcess.ExitCode

if (-not (Test-Path -LiteralPath $validationLog -PathType Leaf)) {
    throw "Physics World P0 PIE validation did not create its log: '$validationLog'."
}
if (Select-String -LiteralPath $validationLog -SimpleMatch "PHYSICS_WORLD_P0_VALIDATION_FAIL" -Quiet) {
    $failure = Select-String -LiteralPath $validationLog -SimpleMatch "PHYSICS_WORLD_P0_VALIDATION_FAIL" | Select-Object -Last 1
    throw "Physics World P0 PIE validation failed: $($failure.Line.Trim()); see '$validationLog'."
}
if ($exitCode -ne 0) {
    throw "Unreal Editor exited with code $exitCode; see '$validationLog'."
}

$success = Select-String -LiteralPath $validationLog -SimpleMatch "PHYSICS_WORLD_P0_VALIDATION_OK" | Select-Object -Last 1
if (-not $success) {
    throw "Physics World P0 PIE validation produced no success marker; see '$validationLog'."
}

$runtimeErrorPatterns = @(
    "PIE: Error:",
    "Accessed None",
    "Blueprint Runtime Error",
    "LogBlueprint: Error",
    "LogScript: Error"
)
$runtimeErrors = @(Select-String -LiteralPath $validationLog -SimpleMatch -Pattern $runtimeErrorPatterns)
if ($runtimeErrors.Count -gt 0) {
    throw "Physics World P0 PIE validation found a runtime error: $($runtimeErrors[0].Line.Trim()); see '$validationLog'."
}

Write-Host $success.Line.Trim()
Write-Host "Physics World P0 PIE validation log: $validationLog"
