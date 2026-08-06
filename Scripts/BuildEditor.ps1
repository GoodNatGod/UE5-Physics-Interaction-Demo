[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EngineRoot,
    [string]$StageRoot = "C:\tmp\RoverReplicaBuild",
    [switch]$Clean,
    [switch]$SkipSync,
    [switch]$SkipWaitMutex
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-Robocopy {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string[]]$Options
    )

    & robocopy.exe $Source $Destination @Options
    if ($LASTEXITCODE -ge 8) {
        throw "robocopy failed with exit code $LASTEXITCODE while copying '$Source'."
    }
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$projectFiles = @(Get-ChildItem -LiteralPath $projectRoot -File -Filter "*.uproject")
if ($projectFiles.Count -ne 1) {
    throw "Expected exactly one .uproject in '$projectRoot'; found $($projectFiles.Count)."
}

$buildScript = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"
if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) {
    throw "Unreal Build Tool entry point was not found at '$buildScript'."
}

$stageRoot = [IO.Path]::GetFullPath($StageRoot)
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
$localTempRoot = [IO.Path]::GetFullPath("C:\tmp").TrimEnd('\') + '\'
$isSafeStageRoot = $stageRoot.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $stageRoot.StartsWith($localTempRoot, [StringComparison]::OrdinalIgnoreCase)
if (-not $isSafeStageRoot) {
    throw "StageRoot must be inside '$tempRoot' or '$localTempRoot'."
}

New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null
if ($Clean) {
    foreach ($generatedDirectory in @("Binaries", "Intermediate")) {
        $generatedPath = Join-Path $stageRoot $generatedDirectory
        if (Test-Path -LiteralPath $generatedPath) {
            Remove-Item -LiteralPath $generatedPath -Recurse -Force
        }
    }
}
New-Item -ItemType Directory -Force -Path (Join-Path $stageRoot "Binaries\Win64") | Out-Null

Invoke-Robocopy -Source (Join-Path $projectRoot "Source") -Destination (Join-Path $stageRoot "Source") -Options @("/MIR", "/R:1", "/W:1", "/NFL", "/NDL", "/NJH", "/NJS", "/NP")
Invoke-Robocopy -Source (Join-Path $projectRoot "Config") -Destination (Join-Path $stageRoot "Config") -Options @("/MIR", "/R:1", "/W:1", "/NFL", "/NDL", "/NJH", "/NJS", "/NP")

Get-ChildItem -LiteralPath $stageRoot -File -Filter "*.uproject" | Remove-Item -Force
$stagedProject = Join-Path $stageRoot $projectFiles[0].Name
Copy-Item -LiteralPath $projectFiles[0].FullName -Destination $stagedProject -Force

$buildArguments = @(
    "RoverReplicaEditor",
    "Win64",
    "Development",
    "-Project=$stagedProject",
    "-NoHotReloadFromIDE",
    "-NoUBA"
)
if ($SkipWaitMutex) {
    $buildArguments += "-NoMutex"
}
else {
    $buildArguments += "-WaitMutex"
}

& $buildScript @buildArguments
if ($LASTEXITCODE -ne 0) {
    throw "Unreal Editor build failed with exit code $LASTEXITCODE."
}

$stagedBinaries = Join-Path $stageRoot "Binaries\Win64"
$projectBinaries = Join-Path $projectRoot "Binaries\Win64"
if (-not (Test-Path -LiteralPath $stagedBinaries -PathType Container)) {
    throw "The build completed without producing '$stagedBinaries'."
}

if ($SkipSync) {
    Write-Host "RoverReplicaEditor built at '$stagedBinaries'; project binary synchronization was skipped."
    return
}

New-Item -ItemType Directory -Force -Path $projectBinaries | Out-Null
Invoke-Robocopy -Source $stagedBinaries -Destination $projectBinaries -Options @("/E", "/R:1", "/W:1", "/NFL", "/NDL", "/NJH", "/NJS", "/NP")

Write-Host "RoverReplicaEditor built and synchronized to '$projectBinaries'."
