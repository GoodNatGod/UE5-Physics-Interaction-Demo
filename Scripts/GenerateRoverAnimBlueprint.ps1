[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EngineRoot,
    [string]$StageRoot = "C:\tmp\RoverReplicaAnimBlueprint"
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
$stageRoot = [IO.Path]::GetFullPath($StageRoot)
$safeRoot = [IO.Path]::GetFullPath("C:\tmp").TrimEnd('\') + '\'
if (-not $stageRoot.StartsWith($safeRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "StageRoot must be inside '$safeRoot'."
}

& (Join-Path $PSScriptRoot "BuildEditor.ps1") `
    -EngineRoot $EngineRoot `
    -StageRoot $stageRoot `
    -SkipSync

$stageContent = Join-Path $stageRoot "Content\Rover"
Invoke-Robocopy `
    -Source (Join-Path $projectRoot "Content\Rover") `
    -Destination $stageContent `
    -Options @("/MIR", "/R:1", "/W:1", "/NFL", "/NDL", "/NJH", "/NJS", "/NP")

# The Rover character CDO loads ABP_Rover during commandlet startup, so the staged
# copy must be absent before Unreal launches; deleting it from Python is too late.
$stagedAnimBlueprint = Join-Path $stageContent "Animations\ABP_Rover.uasset"
if (Test-Path -LiteralPath $stagedAnimBlueprint -PathType Leaf) {
    Remove-Item -LiteralPath $stagedAnimBlueprint -Force
}

Invoke-Robocopy `
    -Source (Join-Path $projectRoot "Content\Input") `
    -Destination (Join-Path $stageRoot "Content\Input") `
    -Options @("/MIR", "/R:1", "/W:1", "/NFL", "/NDL", "/NJH", "/NJS", "/NP")

$stageScripts = Join-Path $stageRoot "Scripts"
New-Item -ItemType Directory -Force -Path $stageScripts | Out-Null
$generateScript = Join-Path $stageScripts "generate_rover_anim_blueprint.py"
$validateScript = Join-Path $stageScripts "validate_rover_anim_blueprint.py"
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "generate_rover_anim_blueprint.py") -Destination $generateScript -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "validate_rover_anim_blueprint.py") -Destination $validateScript -Force

$stagedProjects = @(Get-ChildItem -LiteralPath $stageRoot -File -Filter "*.uproject")
if ($stagedProjects.Count -ne 1) {
    throw "Expected exactly one staged .uproject; found $($stagedProjects.Count)."
}
$editorCommand = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$dataCache = Join-Path $stageRoot "DerivedDataCache\Commandlet"
New-Item -ItemType Directory -Force -Path $dataCache | Out-Null

foreach ($scriptPath in @($generateScript, $validateScript)) {
    & $editorCommand `
        $stagedProjects[0].FullName `
        -run=pythonscript `
        "-script=$scriptPath" `
        -unattended `
        -nop4 `
        -nosplash `
        -NoSound `
        "-LocalDataCachePath=$dataCache"
    if ($LASTEXITCODE -ne 0) {
        throw "Unreal Python commandlet failed with exit code $LASTEXITCODE for '$scriptPath'."
    }
}

$stagedAsset = $stagedAnimBlueprint
$projectAsset = Join-Path $projectRoot "Content\Rover\Animations\ABP_Rover.uasset"
if (-not (Test-Path -LiteralPath $stagedAsset -PathType Leaf)) {
    throw "The validated AnimBlueprint was not generated at '$stagedAsset'."
}
Copy-Item -LiteralPath $stagedAsset -Destination $projectAsset -Force

Write-Host "Generated and independently validated '$projectAsset'."
