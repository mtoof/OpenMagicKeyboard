# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$Analyze,
    [ValidateSet('HidProbe', 'DeviceBinding', 'Gui', 'Driver', 'DriverTests')][string]$Target = 'HidProbe'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if ($Target -eq 'Gui') {
    if ($Analyze) { throw '-Analyze applies to the native C++ tools; GUI profile checks are available with --self-test.' }
    & (Join-Path $PSScriptRoot 'build-gui.ps1') -Configuration $Configuration
    return
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer/vswhere is missing.' }
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Workload.NativeDesktop Microsoft.Windows.DriverKit -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsPath) { throw 'No Visual Studio with Desktop C++ and Windows Driver Kit integration was found.' }
$msbuild = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
$kitsRoot = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots').KitsRoot10
$matchingKits = @(Get-ChildItem -LiteralPath (Join-Path $kitsRoot 'Include') -Directory |
    Where-Object { $_.Name -match '^10\.0\.[0-9]+\.0$' -and
        (Test-Path -LiteralPath (Join-Path $_.FullName 'um\Windows.h')) -and
        (Test-Path -LiteralPath (Join-Path $_.FullName 'km\ntddk.h')) -and
        (Test-Path -LiteralPath (Join-Path $kitsRoot ('build\' + $_.Name + '\WindowsDriver.Common.targets'))) } |
    Sort-Object { [version]$_.Name } -Descending)
if ($matchingKits.Count -eq 0) { throw 'No matching SDK/WDK build was found.' }
$sdkVersion = $matchingKits[0].Name
$executableName = if ($Target -eq 'HidProbe') { 'MagicKeyboardHidProbe' } elseif ($Target -eq 'Driver') { 'OpenMagicKeyboardFilter' } elseif ($Target -eq 'DriverTests') { 'MagicKeyboardDriverTests' } else { 'MagicKeyboardDeviceBinding' }
$project = if ($Target -eq 'Driver') { Join-Path $repoRoot 'driver\OpenMagicKeyboardFilter.vcxproj' } else { Join-Path $repoRoot "tools\$Target\$executableName.vcxproj" }
Write-Host "Building ${Target}: $Configuration x64; SDK/WDK $sdkVersion; $msbuild"
& $msbuild $project /nologo /m /t:Build "/p:Configuration=$Configuration" /p:Platform=x64 "/p:WindowsTargetPlatformVersion=$sdkVersion" "/p:RunCodeAnalysis=$($Analyze.IsPresent.ToString().ToLowerInvariant())" /v:minimal
if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE." }
if ($Target -eq 'Driver') {
    $driverRoot = Join-Path $repoRoot "artifacts\$Configuration\driver"
    $packageRoot = Join-Path $repoRoot "artifacts\$Configuration\package"
    $inf = Join-Path $packageRoot 'OpenMagicKeyboardFilter.inf'
    $infVerifier = Join-Path $kitsRoot "Tools\$sdkVersion\x64\InfVerif.exe"
    & $infVerifier /w /v $inf
    if ($LASTEXITCODE -ne 0) { throw "Standalone InfVerif failed: $LASTEXITCODE" }
    $catalogTool = Join-Path $kitsRoot "bin\$sdkVersion\x86\Inf2Cat.exe"
    & $catalogTool "/driver:$packageRoot" /os:10_CO_X64,10_NI_X64,10_GE_X64 /uselocaltime
    if ($LASTEXITCODE -ne 0) { throw "Inf2Cat signability validation failed: $LASTEXITCODE" }
    Write-Host ('Driver directory: ' + $driverRoot)
    Write-Host ('Unsigned package: ' + $packageRoot)
}
else { Write-Host ('Executable: ' + (Join-Path $repoRoot "artifacts\$Configuration\$executableName.exe")) }
