# Installs the hint timer and camera into the game folder. WFE is not needed.
# The game loads ijl15.dll by itself, so ours takes that name and forwards to the real one (ijl15_orig.dll).
# To undo: delete ijl15.dll and rename ijl15_orig.dll back to ijl15.dll.
# Also moves icons with a wrong letter away, turns off custom hotkeys, caps the frame rate, restores the saved camera.
# Close the game first.
#   powershell -ExecutionPolicy Bypass -File setup\install.ps1 ["C:\path\to\Warcraft 3"]
param([string]$GameDir = 'C:\Program Files (x86)\Warcraft 3', [string]$Dll, [switch]$FilesOnly)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$icons = 'BTNArcher', 'BTNBacktoWork', 'BTNColdArrowsOff', 'BTNColdArrowsOn', 'BTNControlMagic', 'BTNDruidOfTheClaw',
    'BTNDruidOfTheTalon', 'BTNEnsnare', 'BTNFlare', 'BTNGargoyle', 'BTNHeadHunterBerserker', 'BTNMonsoon', 'BTNQuillBeast',
    'BTNReinforcedBurrows', 'BTNReplenishManaOff', 'BTNReplenishManaOn', 'BTNReturnGoods', 'BTNSentinel', 'BTNSpiritWalker',
    'BTNStoneForm', 'BTNWispHealOff', 'BTNWispHealOn'

function Install-Files {
    # Only the first install renames: a second run must not overwrite the real DLL with ours.
    if (!(Test-Path "$GameDir\ijl15_orig.dll")) { Rename-Item "$GameDir\ijl15.dll" 'ijl15_orig.dll' }
    Copy-Item $Dll "$GameDir\ijl15.dll" -Force
    Copy-Item "$repo\orc_hints.txt" "$GameDir\orc_hints.txt" -Force

    # The old setup loaded the DLL through WFE: without this, WFE would load it a second time.
    Remove-Item "$GameDir\Libraries\orc_hints.dll", "$GameDir\Libraries\orc_hints.txt" -ErrorAction SilentlyContinue

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

if (!(Test-Path "$GameDir\ijl15.dll")) { throw "No ijl15.dll in '$GameDir': is this the game folder?" }
if (Get-Process war3 -ErrorAction SilentlyContinue) { throw 'Close the game first.' }

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
Set-ItemProperty "$root\Video" maxfps 75 -Type DWord
if (!(Test-Path "$env:APPDATA\orc_hints.ini")) { Copy-Item "$PSScriptRoot\orc_hints.ini" "$env:APPDATA\orc_hints.ini" }
Write-Output 'OK. Start the game with war3.exe (not through WFE).'
