# SPDX-License-Identifier: MIT
[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$gui = Join-Path $repoRoot "artifacts\$Configuration\MagicKeyboardGui.exe"
if (-not (Test-Path -LiteralPath $gui)) { throw 'Build the GUI before running checks.' }
$testRoot = Join-Path $repoRoot ('artifacts\gui-checks-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
function Invoke-HeadlessGui([string]$Argument, [string]$Name, [int]$ExpectedExit) {
    $stdout = Join-Path $testRoot ($Name + '.stdout.txt')
    $stderr = Join-Path $testRoot ($Name + '.stderr.txt')
    $process = Start-Process -FilePath $gui -ArgumentList $Argument -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if ($process.ExitCode -ne $ExpectedExit) { Get-Content -LiteralPath $stderr; throw "$Name returned $($process.ExitCode)." }
    Get-Content -LiteralPath $stdout
}
Invoke-HeadlessGui '--self-test' 'self-test' 0
Invoke-HeadlessGui '--unknown' 'invalid-cli' 1
Invoke-HeadlessGui '--profile' 'missing-profile-path' 1
[void][Reflection.Assembly]::LoadFrom($gui)
$profilePath = Join-Path $testRoot 'profile.json'
$mappingProfile = [OpenMagicKeyboard.Gui.MappingProfile]::Defaults()
$mappingProfile.Save($profilePath)
$mappingProfile.SwapControlsAndWindows()
$mappingProfile.Set('Fn', [OpenMagicKeyboard.Gui.KeyTarget]::F23)
$mappingProfile.Save($profilePath)
$loaded = [OpenMagicKeyboard.Gui.MappingProfile]::Load($profilePath)
if ($loaded.Get('Fn') -ne [OpenMagicKeyboard.Gui.KeyTarget]::F23 -or $loaded.Get('LeftCtrl') -ne [OpenMagicKeyboard.Gui.KeyTarget]::LeftWindows) { throw 'Profile update/reload failed.' }
$validHash = (Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash
$mappingProfile.Device.InterfaceNumber = 0
$rejected = $false
try { $mappingProfile.Save($profilePath) } catch { $rejected = $true }
if (-not $rejected) { throw 'MI_00 profile save was accepted.' }
if ((Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash -ne $validHash) { throw 'Rejected save changed the existing profile.' }
if (@(Get-ChildItem -LiteralPath $testRoot -Filter '*.tmp').Count -ne 0) { throw 'Profile save left temporary files.' }
Write-Host 'GUI CLI, atomic profile save/update, reload and rejected-save preservation checks passed.'
Write-Host "Check artifacts: $testRoot"
