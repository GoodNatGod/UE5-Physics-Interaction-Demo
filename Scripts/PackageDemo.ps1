[CmdletBinding()]
param(
    [string]$EngineRoot = "D:\unreal\UE_5.8",
    [string]$StageRoot = "D:\tmp\RoverReplicaPackageSource",
    [string]$DdcRoot = "D:\tmp\RoverReplicaPackageDDC",
    [string]$OutputRoot = "",
    [ValidateSet("Development", "Shipping")][string]$Configuration = "Development",
    [string]$MapPath = "/Game/PhysicsWorldDemo/Maps/L_PhysicsWorldDemo_Lumen",
    [switch]$CleanStage
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-SafeTemporaryPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $resolvedPath = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $allowedRoots = @(
        [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\'),
        [IO.Path]::GetFullPath("C:\tmp").TrimEnd('\'),
        [IO.Path]::GetFullPath("D:\tmp").TrimEnd('\')
    ) | Select-Object -Unique

    foreach ($allowedRoot in $allowedRoots) {
        $allowedPrefix = $allowedRoot + '\'
        if ($resolvedPath.StartsWith($allowedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            return $resolvedPath
        }
    }

    throw "$Label must be a child directory of: $($allowedRoots -join ', '). Received '$resolvedPath'."
}

function Invoke-Robocopy {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    & robocopy.exe $Source $Destination /MIR /COPY:DAT /DCOPY:DAT /R:1 /W:1 /MT:16 /NFL /NDL /NJH /NJS /NP
    if ($LASTEXITCODE -ge 8) {
        throw "robocopy failed with exit code $LASTEXITCODE while mirroring '$Source'."
    }
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$projectFiles = @(Get-ChildItem -LiteralPath $projectRoot -File -Filter "*.uproject")
if ($projectFiles.Count -ne 1) {
    throw "Expected exactly one .uproject in '$projectRoot'; found $($projectFiles.Count)."
}

$runUat = Join-Path $EngineRoot "Engine\Build\BatchFiles\RunUAT.bat"
if (-not (Test-Path -LiteralPath $runUat -PathType Leaf)) {
    throw "Unreal Automation Tool entry point was not found at '$runUat'."
}

foreach ($requiredDirectory in @("Source", "Config", "Content")) {
    $requiredPath = Join-Path $projectRoot $requiredDirectory
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Container)) {
        throw "Required project directory was not found: '$requiredPath'."
    }
}

$stageRoot = Assert-SafeTemporaryPath -Path $StageRoot -Label "StageRoot"
$ddcRoot = Assert-SafeTemporaryPath -Path $DdcRoot -Label "DdcRoot"
if ($stageRoot.Equals($ddcRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "StageRoot and DdcRoot must use separate directories."
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputRoot = "D:\UE5Builds\RoverPhysicsDemo-Win64-$Configuration-$timestamp"
}
$outputRoot = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
if ($outputRoot.Equals($projectRoot.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase) -or
    $outputRoot.StartsWith($projectRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must be outside the project workspace. Received '$outputRoot'."
}
if (Test-Path -LiteralPath $outputRoot) {
    $existingEntries = @(Get-ChildItem -LiteralPath $outputRoot -Force)
    if ($existingEntries.Count -gt 0) {
        throw "OutputRoot already contains files; choose a new directory: '$outputRoot'."
    }
}

New-Item -ItemType Directory -Force -Path $stageRoot, $ddcRoot, $outputRoot | Out-Null
if ($CleanStage) {
    foreach ($generatedDirectory in @("Binaries", "Intermediate", "Saved")) {
        $generatedPath = Join-Path $stageRoot $generatedDirectory
        if (Test-Path -LiteralPath $generatedPath) {
            Remove-Item -LiteralPath $generatedPath -Recurse -Force
        }
    }
}

foreach ($directoryName in @("Source", "Config", "Content")) {
    Invoke-Robocopy `
        -Source (Join-Path $projectRoot $directoryName) `
        -Destination (Join-Path $stageRoot $directoryName)
}
foreach ($optionalDirectory in @("Build", "Plugins")) {
    $optionalSource = Join-Path $projectRoot $optionalDirectory
    if (Test-Path -LiteralPath $optionalSource -PathType Container) {
        Invoke-Robocopy `
            -Source $optionalSource `
            -Destination (Join-Path $stageRoot $optionalDirectory)
    }
}

Get-ChildItem -LiteralPath $stageRoot -File -Filter "*.uproject" | Remove-Item -Force
$stagedProject = Join-Path $stageRoot "RoverReplica.uproject"
Copy-Item -LiteralPath $projectFiles[0].FullName -Destination $stagedProject -Force

$packageLog = Join-Path $outputRoot "PackageDemo.log"
$uatArguments = @(
    "BuildCookRun",
    "-Project=$stagedProject",
    "-Target=RoverReplica",
    "-Platform=Win64",
    "-ClientConfig=$Configuration",
    "-Map=$MapPath",
    "-Build",
    "-Cook",
    "-Stage",
    "-Package",
    "-Pak",
    "-IoStore",
    "-Compressed",
    "-Archive",
    "-ArchiveDirectory=$outputRoot",
    "-NoP4",
    "-NoDebugInfo",
    "-Unattended",
    "-UTF8Output",
    "-AdditionalCookerOptions=-SkipZenStore",
    "-UbtArgs=-NoUBA"
)

$processTemp = Join-Path $stageRoot "Temp"
New-Item -ItemType Directory -Force -Path $processTemp | Out-Null
$previousTemp = $env:TEMP
$previousTmp = $env:TMP
$previousLocalDdc = [Environment]::GetEnvironmentVariable("UE-LocalDataCachePath", "Process")

try {
    $env:TEMP = $processTemp
    $env:TMP = $processTemp
    [Environment]::SetEnvironmentVariable("UE-LocalDataCachePath", $ddcRoot, "Process")

    & $runUat @uatArguments 2>&1 | Tee-Object -FilePath $packageLog
    $uatExitCode = $LASTEXITCODE
}
finally {
    $env:TEMP = $previousTemp
    $env:TMP = $previousTmp
    [Environment]::SetEnvironmentVariable("UE-LocalDataCachePath", $previousLocalDdc, "Process")
}

if ($uatExitCode -ne 0) {
    throw "Demo packaging failed with exit code $uatExitCode. See '$packageLog'."
}

$launchers = @(
    Get-ChildItem -LiteralPath $outputRoot -Recurse -File -Filter "RoverReplica.exe" |
        Where-Object { $_.FullName -notmatch '\\Binaries\\' }
)
if ($launchers.Count -ne 1) {
    throw "Packaging completed but expected one RoverReplica.exe launcher; found $($launchers.Count). See '$outputRoot'."
}

$vcRedist = Join-Path $EngineRoot "Engine\Extras\Redist\en-us\vc_redist.x64.exe"
if (Test-Path -LiteralPath $vcRedist -PathType Leaf) {
    $prerequisiteDirectory = Join-Path $outputRoot "Prerequisites"
    New-Item -ItemType Directory -Force -Path $prerequisiteDirectory | Out-Null
    Copy-Item -LiteralPath $vcRedist -Destination $prerequisiteDirectory -Force
}

$packageBytes = (Get-ChildItem -LiteralPath $outputRoot -Recurse -File | Measure-Object -Property Length -Sum).Sum
$packageSizeGb = [math]::Round($packageBytes / 1GB, 2)
Write-Host "ROVER_DEMO_PACKAGE_OK"
Write-Host "Executable: $($launchers[0].FullName)"
Write-Host "Output: $outputRoot"
Write-Host "Size: $packageSizeGb GB"
Write-Host "Map: $MapPath"
Write-Host "Configuration: $Configuration"
Write-Host "Log: $packageLog"
