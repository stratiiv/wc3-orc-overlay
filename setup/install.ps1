# Puts the same setup on another PC: the DLL and hints in WFE's Libraries, the WFE config,
# no icons with a wrong letter, default hotkeys, the saved camera.
# Needs Warcraft III 1.26a and WFE in the game folder. Close the game and WFE first.
#   powershell -ExecutionPolicy Bypass -File setup\install.ps1 ["C:\path\to\Warcraft 3"]
param([string]$GameDir = 'C:\Program Files (x86)\Warcraft 3', [string]$Dll, [switch]$FilesOnly)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$icons = 'BTNArcher', 'BTNBacktoWork', 'BTNColdArrowsOff', 'BTNColdArrowsOn', 'BTNControlMagic', 'BTNDruidOfTheClaw',
    'BTNDruidOfTheTalon', 'BTNEnsnare', 'BTNFlare', 'BTNGargoyle', 'BTNHeadHunterBerserker', 'BTNMonsoon', 'BTNQuillBeast',
    'BTNReinforcedBurrows', 'BTNReplenishManaOff', 'BTNReplenishManaOn', 'BTNReturnGoods', 'BTNSentinel', 'BTNSpiritWalker',
    'BTNStoneForm', 'BTNWispHealOff', 'BTNWispHealOn'

function Install-Files {
    # Undo the short-lived setup that replaced ijl15.dll: put the real DLL back.
    if (Test-Path "$GameDir\ijl15_orig.dll") { Move-Item "$GameDir\ijl15_orig.dll" "$GameDir\ijl15.dll" -Force }
    Remove-Item "$GameDir\orc_hints.txt" -ErrorAction SilentlyContinue

    New-Item -ItemType Directory -Force "$GameDir\Libraries", "$GameDir\Profiles" | Out-Null
    Copy-Item $Dll "$GameDir\Libraries\orc_hints.dll" -Force
    Copy-Item "$repo\orc_hints.txt" "$GameDir\Libraries\orc_hints.txt" -Force

    $ini = "$GameDir\Profiles\WFEConfig.ini"
    if (Test-Path $ini) { Copy-Item $ini "$ini.before_orc_hints" -Force }
    $lines = foreach ($l in [IO.File]::ReadAllLines("$PSScriptRoot\WFEConfig.ini")) {
        if ($l.StartsWith('GAMEPATH =')) { "GAMEPATH = $GameDir\war3.exe" }
        elseif ($l.StartsWith('LIBRARIESPATH =')) { "LIBRARIESPATH = $GameDir\Libraries\" }
        else { $l }
    }
    [IO.File]::WriteAllLines($ini, [string[]]$lines)

    # Baked-in letters that disagree with the game's hotkeys; the game falls back to its own icon.
    $bak = "$GameDir\ReplaceableTextures_wrong_letters"
    foreach ($pair in @(@('CommandButtons', ''), @('CommandButtonsDisabled', 'DIS'))) {
        foreach ($i in $icons) {
            $from = "$GameDir\ReplaceableTextures\$($pair[0])\$($pair[1])$i.blp"
            if (Test-Path $from) {
                New-Item -ItemType Directory -Force "$bak\$($pair[0])" | Out-Null
                Move-Item $from "$bak\$($pair[0])\" -Force
            }
        }
    }
}

if ($FilesOnly) { Install-Files; exit 0 }

if (!(Test-Path "$GameDir\WFEDll.dll")) { throw "WFE not found in '$GameDir'. Copy the game folder with WFE first." }
if (Get-Process war3, WFEApp -ErrorAction SilentlyContinue) { throw 'Close the game and WFE first: WFE can overwrite its config.' }

if (!$Dll) { $Dll = "$repo\orc_hints.dll" }
if (!(Test-Path $Dll)) {
    $Dll = "$env:TEMP\orc_hints_release.dll"
    [Net.ServicePointManager]::SecurityProtocol = 'Tls12'
    Invoke-WebRequest 'https://github.com/stratiiv/wc3-orc-overlay/releases/download/v1.0/orc_hints.dll' -OutFile $Dll -UseBasicParsing
}

$writable = $false
try { [IO.File]::Create("$GameDir\.orc_write_test", 1, 'DeleteOnClose').Dispose(); $writable = $true } catch {}
if ($writable) { Install-Files }
else {
    $argv = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -GameDir `"$GameDir`" -Dll `"$Dll`" -FilesOnly"
    $p = Start-Process powershell -Verb RunAs -Wait -PassThru -ArgumentList $argv
    if ($p.ExitCode) { throw "Installing into the game folder failed (exit $($p.ExitCode))." }
}

# Per-user settings: outside the elevated part, so they land in this user's profile.
$root = 'HKCU:\Software\Blizzard Entertainment\Warcraft III'
foreach ($k in 'Gameplay', 'Video') { if (!(Test-Path "$root\$k")) { New-Item "$root\$k" -Force | Out-Null } }
Set-ItemProperty "$root\Gameplay" customkeys 0 -Type DWord
Set-ItemProperty "$root\Video" maxfps 200 -Type DWord
if (!(Test-Path "$env:APPDATA\orc_hints.ini")) { Copy-Item "$PSScriptRoot\orc_hints.ini" "$env:APPDATA\orc_hints.ini" }
Write-Output 'OK. Start the game with WFE: Launch.'
