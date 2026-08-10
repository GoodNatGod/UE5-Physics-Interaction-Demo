[CmdletBinding()]
param(
    [string]$CharacterSourceRoot = "D:\BaiduNetdiskDownload\艾斯3d建模-鸣潮 漂泊者 男＆女-标准版\艾斯3d建模-鸣潮 漂泊者 男＆女-标准版\Document-FbxFormat",
    [string]$WeaponFbx = "D:\BaiduNetdiskDownload\艾斯3d建模-鸣潮 通用类型武器-附赠品\刀剑类型-01\R2Sword001.fbx",
    [string]$EngineRoot = "D:\unreal\UE_5.8"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$projectFiles = @(Get-ChildItem -LiteralPath $projectRoot -File -Filter "*.uproject")
if ($projectFiles.Count -ne 1) {
    throw "Expected exactly one .uproject in '$projectRoot'; found $($projectFiles.Count)."
}

$characterSourceRoot = [IO.Path]::GetFullPath($CharacterSourceRoot)
$weaponFbx = [IO.Path]::GetFullPath($WeaponFbx)
if (-not (Test-Path -LiteralPath $characterSourceRoot -PathType Container)) {
    throw "Character FBX source directory was not found: '$characterSourceRoot'."
}
if (-not (Test-Path -LiteralPath $weaponFbx -PathType Leaf)) {
    throw "Weapon FBX was not found: '$weaponFbx'."
}
if (Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue) {
    throw "Close Unreal Editor before importing P0 combat assets."
}

$editorCommand = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (-not (Test-Path -LiteralPath $editorCommand -PathType Leaf)) {
    throw "Unreal Editor commandlet was not found at '$editorCommand'."
}

$localDataCache = Join-Path $projectRoot "DerivedDataCache\Commandlet"
New-Item -ItemType Directory -Force -Path $localDataCache | Out-Null

$previousCharacterRoot = $env:ROVER_COMBAT_CHARACTER_FBX_ROOT
$previousWeaponFbx = $env:ROVER_COMBAT_WEAPON_FBX
try {
    $env:ROVER_COMBAT_CHARACTER_FBX_ROOT = $characterSourceRoot
    $env:ROVER_COMBAT_WEAPON_FBX = $weaponFbx
    & $editorCommand `
        $projectFiles[0].FullName `
        -run=pythonscript `
        "-script=$PSScriptRoot\import_rover_combat_p0.py" `
        -unattended `
        -nop4 `
        -nosplash `
        -NoSound `
        "-LocalDataCachePath=$localDataCache"
    if ($LASTEXITCODE -ne 0) {
        throw "Rover combat P0 import failed with exit code $LASTEXITCODE."
    }
}
finally {
    $env:ROVER_COMBAT_CHARACTER_FBX_ROOT = $previousCharacterRoot
    $env:ROVER_COMBAT_WEAPON_FBX = $previousWeaponFbx
}

Write-Host "Rover combat P0 animations, weapon, Montages, config, and training enemy were imported successfully."
