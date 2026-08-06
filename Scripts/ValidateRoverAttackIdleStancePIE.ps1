[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EngineRoot,
    [ValidateRange(1, 3)][int]$TargetAttack = 1,
    [ValidateRange(30, 600)][int]$TimeoutSeconds = 120
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$projectFiles = @(Get-ChildItem -LiteralPath $projectRoot -File -Filter "*.uproject")
if ($projectFiles.Count -ne 1) {
    throw "Expected exactly one .uproject in '$projectRoot'; found $($projectFiles.Count)."
}

$scriptPath = Join-Path $PSScriptRoot "validate_rover_attack_idle_stance.py"
if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
    throw "Attack idle-stance PIE validation script was not found: '$scriptPath'."
}

$editorCommand = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (-not (Test-Path -LiteralPath $editorCommand -PathType Leaf)) {
    throw "Unreal Editor command was not found at '$editorCommand'."
}
if (Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue) {
    throw "Close Unreal Editor before running the attack idle-stance PIE validation."
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$validationLog = Join-Path ([IO.Path]::GetTempPath()) "RoverReplica-AttackIdleStance-PIE-$timestamp-$PID.log"
$localDataCache = Join-Path $projectRoot "DerivedDataCache\Validation"
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
    "-RoverAttackIdleTarget=$TargetAttack",
    "-stdout",
    "-FullStdOutLogOutput",
    "-ABSLOG=$validationLog",
    "-LocalDataCachePath=$localDataCache"
)

$editorProcess = Start-Process `
    -FilePath $editorCommand `
    -ArgumentList $editorArguments `
    -PassThru `
    -WindowStyle Hidden

if (-not $editorProcess.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $editorProcess.Id -Force -ErrorAction SilentlyContinue
    throw "Attack idle-stance PIE validation timed out after $TimeoutSeconds seconds; see '$validationLog'."
}
$editorProcess.WaitForExit()
$exitCode = $editorProcess.ExitCode

if (-not (Test-Path -LiteralPath $validationLog -PathType Leaf)) {
    throw "Attack idle-stance PIE validation did not create its log: '$validationLog'."
}
$failureMarker = "ROVER_ATTACK_IDLE_STANCE_PIE_FAIL"
$failure = Select-String -LiteralPath $validationLog -SimpleMatch $failureMarker -ErrorAction SilentlyContinue | Select-Object -Last 1
if ($failure) {
    throw "Attack idle-stance PIE validation failed: $($failure.Line.Trim()); see '$validationLog'."
}
if ($exitCode -ne 0) {
    throw "Unreal Editor exited with code $exitCode; see '$validationLog'."
}

$successMarker = "ROVER_ATTACK_IDLE_STANCE_PIE_OK"
$success = Select-String -LiteralPath $validationLog -SimpleMatch $successMarker | Select-Object -Last 1
if (-not $success) {
    throw "Attack idle-stance PIE validation produced no success marker; see '$validationLog'."
}

$runtimeErrorPatterns = @(
    "PIE: Error:",
    "Accessed None",
    "Blueprint Runtime Error",
    "LogBlueprint: Error",
    "LogScript: Error",
    "RoverCharacter requires an EnhancedInputComponent"
)
$runtimeErrors = @(Select-String -LiteralPath $validationLog -SimpleMatch -Pattern $runtimeErrorPatterns)
if ($runtimeErrors.Count -gt 0) {
    throw "Attack idle-stance PIE validation found a runtime error: $($runtimeErrors[0].Line.Trim()); see '$validationLog'."
}

Write-Host $success.Line.Trim()
Write-Host "Attack idle-stance PIE validation log: $validationLog"
