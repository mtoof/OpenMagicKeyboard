[CmdletBinding()]
param([ValidateSet('Release','Debug')][string]$Configuration='Release',[string]$DescriptorPath)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Workload.NativeDesktop -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vsPath) { throw 'Visual Studio C++ tools missing.' }
$version=[IO.File]::ReadAllText((Join-Path $vsPath 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt')).Trim()
$asan=Join-Path $vsPath "VC\Tools\MSVC\$version\bin\Hostx64\x64"
$priorPath=$env:PATH
try {
    $env:PATH=$asan+';'+$priorPath
    $testTool=Join-Path $repoRoot "artifacts\$Configuration\MagicKeyboardDriverTests.exe"
    if ($DescriptorPath) {
        $descriptorFile=(Resolve-Path -LiteralPath $DescriptorPath -ErrorAction Stop).ProviderPath
        & $testTool --descriptor $descriptorFile
        if ($LASTEXITCODE -ne 0) { throw "Descriptor is unsupported or could not be read (exit $LASTEXITCODE). Mappings must remain disabled." }
    } else {
        & $testTool
        if ($LASTEXITCODE -ne 0) { throw "AddressSanitizer host tests failed: $LASTEXITCODE" }
    }
} finally { $env:PATH=$priorPath }
if ($DescriptorPath) { Write-Host 'Descriptor parsed with AddressSanitizer. Only a local configuration file was read; no keyboard opened.' }
else { Write-Host 'Shared parser/remapper tests passed with AddressSanitizer. No hardware or installation performed.' }
