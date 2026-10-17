[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$frameworkRoot = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$compiler = Join-Path $frameworkRoot 'csc.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw '.NET Framework x64 C# compiler is missing.' }
$release = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\NET Framework Setup\NDP\v4\Full').Release
if ($release -lt 528040) { throw '.NET Framework 4.8 or later is required.' }
$sourceRoot = Join-Path $repoRoot 'tools\Gui'
$outputRoot = Join-Path $repoRoot "artifacts\$Configuration"
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$executable = Join-Path $outputRoot 'MagicKeyboardGui.exe'
$arguments = @('/nologo', '/target:winexe', '/platform:x64', '/warn:4', '/warnaserror+', '/checked+',
    "/out:$executable", ('/win32manifest:' + (Join-Path $sourceRoot 'app.manifest')),
    ('/reference:' + (Join-Path $frameworkRoot 'System.Windows.Forms.dll')),
    ('/reference:' + (Join-Path $frameworkRoot 'System.Drawing.dll')),
    ('/reference:' + (Join-Path $frameworkRoot 'System.Management.dll')),
    ('/reference:' + (Join-Path $frameworkRoot 'System.Runtime.Serialization.dll')),
    ('/win32icon:' + (Join-Path $sourceRoot 'app.ico')), ('/resource:' + (Join-Path $sourceRoot 'app.ico') + ',OpenMagicKeyboard.app.ico'))
if ($Configuration -eq 'Debug') { $arguments += '/debug+'; $arguments += '/optimize-' }
else { $arguments += '/debug:pdbonly'; $arguments += '/optimize+' }
$arguments += @('MappingProfile.cs', 'AppShell.cs', 'DeviceStatus.cs', 'Battery.cs', 'KeyboardView.cs', 'DriverClient.cs', 'FnTestWindow.cs', 'F23TestWindow.cs', 'DisplayBrightness.cs', 'MediaHelper.cs', 'MainWindow.cs', 'Program.cs' | ForEach-Object { Join-Path $sourceRoot $_ })
& $compiler @arguments
if ($LASTEXITCODE -ne 0) { throw "GUI compiler failed with exit code $LASTEXITCODE." }
Write-Host "GUI: $executable"
