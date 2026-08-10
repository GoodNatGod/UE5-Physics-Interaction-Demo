[CmdletBinding()]
param(
    [string]$EngineRoot = "D:\unreal\UE_5.8",
    [string]$OutputRoot = "",
    [ValidateRange(60, 600)][int]$TimeoutSeconds = 240
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$projectFiles = @(Get-ChildItem -LiteralPath $projectRoot -File -Filter "*.uproject")
if ($projectFiles.Count -ne 1) {
    throw "Expected exactly one .uproject in '$projectRoot'; found $($projectFiles.Count)."
}

$scriptPath = Join-Path $PSScriptRoot "capture_portfolio_demo.py"
if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
    throw "Portfolio capture script was not found: '$scriptPath'."
}

$editorCommand = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (-not (Test-Path -LiteralPath $editorCommand -PathType Leaf)) {
    throw "Unreal Editor command was not found at '$editorCommand'."
}
if (Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue) {
    throw "Close Unreal Editor before running the portfolio capture."
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $projectRoot "Saved\PortfolioCapture"
}
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$captureDirectory = [IO.Path]::GetFullPath((Join-Path $OutputRoot $timestamp))
New-Item -ItemType Directory -Force -Path $captureDirectory | Out-Null

$captureLog = Join-Path ([IO.Path]::GetTempPath()) "PhysicsWorld-PortfolioCapture-$timestamp-$PID.log"
$localDataCache = Join-Path $projectRoot "DerivedDataCache\Capture"
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
    "-ABSLOG=$captureLog",
    "-LocalDataCachePath=$localDataCache",
    "-PortfolioCaptureDir=$captureDirectory"
)

$editorProcess = Start-Process `
    -FilePath $editorCommand `
    -ArgumentList $editorArguments `
    -PassThru `
    -WindowStyle Hidden

if (-not $editorProcess.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $editorProcess.Id -Force -ErrorAction SilentlyContinue
    throw "Portfolio capture timed out after $TimeoutSeconds seconds; see '$captureLog'."
}
$editorProcess.WaitForExit()
$exitCode = $editorProcess.ExitCode

if (-not (Test-Path -LiteralPath $captureLog -PathType Leaf)) {
    throw "Portfolio capture did not create its log: '$captureLog'."
}
$failure = Select-String -LiteralPath $captureLog -SimpleMatch "PORTFOLIO_CAPTURE_FAIL" -ErrorAction SilentlyContinue | Select-Object -Last 1
if ($failure) {
    throw "Portfolio capture failed: $($failure.Line.Trim()); see '$captureLog'."
}
if ($exitCode -ne 0) {
    throw "Unreal Editor exited with code $exitCode; see '$captureLog'."
}

$success = Select-String -LiteralPath $captureLog -SimpleMatch "PORTFOLIO_CAPTURE_OK" | Select-Object -Last 1
if (-not $success) {
    throw "Portfolio capture produced no success marker; see '$captureLog'."
}

$frames = @(Get-ChildItem -LiteralPath $captureDirectory -File -Filter "*.bmp" | Sort-Object Name)
$durationMatch = [regex]::Match($success.Line, "duration=(?<duration>[0-9]+(?:\.[0-9]+)?)s")
if (-not $durationMatch.Success) {
    throw "Portfolio capture success marker has no duration: '$($success.Line.Trim())'."
}
$duration = [double]::Parse(
    $durationMatch.Groups["duration"].Value,
    [Globalization.CultureInfo]::InvariantCulture)
if ([Math]::Abs($duration - 8.5) -gt 0.25) {
    throw "Expected an 8.5s capture (+/-0.25s); reported $duration seconds."
}
$reportedFrameMatch = [regex]::Match($success.Line, "frames=(?<frames>[0-9]+)")
if (-not $reportedFrameMatch.Success) {
    throw "Portfolio capture success marker has no frame count: '$($success.Line.Trim())'."
}
$reportedFrameCount = [int]$reportedFrameMatch.Groups["frames"].Value
if ($frames.Count -ne $reportedFrameCount) {
    throw "Capture reported $reportedFrameCount frames but found $($frames.Count) bitmap files in '$captureDirectory'."
}
$effectiveFps = $frames.Count / $duration
if ($effectiveFps -lt 11.5) {
    throw "Portfolio capture rate was too low: $($effectiveFps.ToString('F2')) FPS over $($duration.ToString('F2'))s."
}

Write-Host $success.Line.Trim()
Write-Host "Portfolio capture frames: $captureDirectory ($($frames.Count) frames, $($effectiveFps.ToString('F2')) FPS)"
Write-Host "Portfolio capture log: $captureLog"
