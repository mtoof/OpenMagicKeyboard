# SPDX-License-Identifier: MIT
[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
$assembly = [Reflection.Assembly]::LoadFrom((Join-Path $repoRoot "artifacts\$Configuration\MagicKeyboardGui.exe"))
$previewRoot = Join-Path $repoRoot ('artifacts\gui-preview-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $previewRoot | Out-Null
$previewProfile = [string](Join-Path $previewRoot 'profile.json')
[Windows.Forms.Application]::EnableVisualStyles()
$type = $assembly.GetType('OpenMagicKeyboard.Gui.MainWindow', $true)
$constructor = $type.GetConstructor([Reflection.BindingFlags]'Instance,NonPublic', $null, [type[]]@([string]), $null)
$window = $constructor.Invoke([object[]]@($previewProfile))
function Visit-Controls($Container) {
    foreach ($child in $Container.Controls) { Write-Output -NoEnumerate $child; Visit-Controls $child }
}
try {
    $window.Show()
    [Windows.Forms.Application]::DoEvents()
    $controls = @(Visit-Controls $window)
    $swap = $controls | Where-Object { $_ -is [Windows.Forms.Button] -and $_.Text -like 'Swap ctrl*cmd' }
    $apply = $controls | Where-Object { $_ -is [Windows.Forms.Button] -and $_.Text -like '*Apply to keyboard' }
    $fn = $controls | Where-Object { $_ -is [Windows.Forms.ComboBox] -and $_.AccessibleName -eq 'Mapping for fn / Globe' }
    $caps = $controls | Where-Object { $_ -is [Windows.Forms.ComboBox] -and $_.AccessibleName -eq 'Mapping for Caps Lock' }
    $leftShift = $controls | Where-Object { $_ -is [Windows.Forms.ComboBox] -and $_.AccessibleName -eq 'Mapping for Left Shift' }
    $fnCombos = $controls | Where-Object { $_ -is [Windows.Forms.CheckBox] -and $_.Text -like 'fn + delete*' }
    $fkeys = $controls | Where-Object { $_ -is [Windows.Forms.ComboBox] -and $_.Items.Count -eq 3 }
    if (-not $swap -or -not $fn -or -not $apply -or -not $caps -or -not $leftShift -or -not $fnCombos -or -not $fkeys) { throw 'Required GUI controls missing.' }
    $swap.PerformClick()
    $fn.SelectedIndex = 8 # F23
    $caps.SelectedIndex = 1 # left ctrl
    $fkeys.SelectedIndex = 2
    $fnCombos.Checked = $true
    [void]$type.GetMethod('Save', [Reflection.BindingFlags]'Instance,NonPublic').Invoke($window, @())
    [Windows.Forms.Application]::DoEvents()
    $saved = [OpenMagicKeyboard.Gui.MappingProfile]::Load($previewProfile)
    if ($saved.Get('Fn') -ne [OpenMagicKeyboard.Gui.KeyTarget]::F23 -or $saved.Get('LeftCtrl') -ne [OpenMagicKeyboard.Gui.KeyTarget]::LeftWindows -or $saved.FKeyMode -ne 2 -or -not $saved.FnCombinations) { throw 'GUI controls did not persist selections.' }
    $bitmap = New-Object Drawing.Bitmap($window.Width, $window.Height)
    try {
        $window.DrawToBitmap($bitmap, (New-Object Drawing.Rectangle(0,0,$window.Width,$window.Height)))
        $bitmap.Save((Join-Path $previewRoot 'gui.png'), [Drawing.Imaging.ImageFormat]::Png)
    } finally { $bitmap.Dispose() }
    Write-Host 'GUI control/save smoke check passed; Apply was never clicked.'
    Write-Host (Join-Path $previewRoot 'gui.png')
} finally { $window.Dispose() }
