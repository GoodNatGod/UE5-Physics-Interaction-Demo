[CmdletBinding()]
param(
    [string]$EngineRoot = "D:\unreal\UE_5.8",
    [string]$OutputPath,
    [switch]$Baseline,
    [ValidateRange(30, 300)][int]$TimeoutSeconds = 120
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$projectFiles = @(Get-ChildItem -LiteralPath $projectRoot -File -Filter "*.uproject")
if ($projectFiles.Count -ne 1) {
    throw "Expected exactly one .uproject in '$projectRoot'; found $($projectFiles.Count)."
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $projectRoot "Saved\Screenshots\WindowsEditor\LooseDebrisPreview.png"
}
$resolvedOutput = [IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $resolvedOutput
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$scriptPath = Join-Path $PSScriptRoot "capture_physics_world_loose_debris_preview.py"
$editorCommand = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (-not (Test-Path -LiteralPath $editorCommand -PathType Leaf)) {
    throw "Unreal Editor command was not found at '$editorCommand'."
}
if (Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue) {
    throw "Close Unreal Editor before capturing the loose-debris preview."
}

$previousOutput = [Environment]::GetEnvironmentVariable(
    "ROVER_LOOSE_DEBRIS_SCREENSHOT", [EnvironmentVariableTarget]::Process
)
$previousBaseline = [Environment]::GetEnvironmentVariable(
    "ROVER_LOOSE_DEBRIS_BASELINE", [EnvironmentVariableTarget]::Process
)
try {
    [Environment]::SetEnvironmentVariable(
        "ROVER_LOOSE_DEBRIS_SCREENSHOT",
        $resolvedOutput.Replace("\", "/"),
        [EnvironmentVariableTarget]::Process
    )
    [Environment]::SetEnvironmentVariable(
        "ROVER_LOOSE_DEBRIS_BASELINE",
        $(if ($Baseline) { "1" } else { "0" }),
        [EnvironmentVariableTarget]::Process
    )
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $captureLog = Join-Path ([IO.Path]::GetTempPath()) "PhysicsWorld-LooseDebris-Preview-$timestamp-$PID.log"
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
        "-ABSLOG=$captureLog"
    )
    $editorProcess = Start-Process `
        -FilePath $editorCommand `
        -ArgumentList $editorArguments `
        -PassThru `
        -WindowStyle Hidden
    if (-not $editorProcess.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $editorProcess.Id -Force -ErrorAction SilentlyContinue
        throw "Loose-debris preview timed out; see '$captureLog'."
    }
    $editorProcess.WaitForExit()
    if ($editorProcess.ExitCode -ne 0) {
        throw "Unreal Editor exited with code $($editorProcess.ExitCode); see '$captureLog'."
    }
    $failure = Select-String -LiteralPath $captureLog -SimpleMatch "PHYSICS_WORLD_LOOSE_DEBRIS_PREVIEW_FAIL" | Select-Object -Last 1
    if ($failure) {
        throw "Loose-debris preview failed: $($failure.Line.Trim()); see '$captureLog'."
    }
    $success = Select-String -LiteralPath $captureLog -SimpleMatch "PHYSICS_WORLD_LOOSE_DEBRIS_PREVIEW_OK" | Select-Object -Last 1
    if (-not $success -or -not (Test-Path -LiteralPath $resolvedOutput -PathType Leaf)) {
        throw "Loose-debris preview produced no screenshot; see '$captureLog'."
    }
    Write-Host $success.Line.Trim()
    Write-Host "Loose-debris preview screenshot: $resolvedOutput"
}
finally {
    [Environment]::SetEnvironmentVariable(
        "ROVER_LOOSE_DEBRIS_SCREENSHOT",
        $previousOutput,
        [EnvironmentVariableTarget]::Process
    )
    [Environment]::SetEnvironmentVariable(
        "ROVER_LOOSE_DEBRIS_BASELINE",
        $previousBaseline,
        [EnvironmentVariableTarget]::Process
    )
}
