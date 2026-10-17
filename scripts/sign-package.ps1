[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidatePattern('^[A-Fa-f0-9]{40}$')][string]$Thumbprint,
    [ValidateSet('Release','Debug')][string]$Configuration='Release',[switch]$Execute)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'package-common.ps1')
$p=Read-ProjectPackage (Join-Path $repoRoot "artifacts\$Configuration\package")
$cert=Get-Item -LiteralPath ('Cert:\CurrentUser\My\'+$Thumbprint) -ErrorAction Stop
if (-not $cert.HasPrivateKey -or $cert.NotAfter -le [DateTime]::Now -or
    @($cert.EnhancedKeyUsageList | Where-Object { $_.ObjectId -eq '1.3.6.1.5.5.7.3.3' }).Count -eq 0) { throw 'Requires an unexpired code-signing certificate and private key.' }
if (-not $Execute) { Write-Host "PLAN ONLY: SHA256 sign SYS, regenerate CAT, then sign CAT with $Thumbprint. No trust/boot changes."; return }
$kits=(Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots').KitsRoot10
$sign=Join-Path $kits 'bin\10.0.28000.0\x64\signtool.exe'
& $sign sign /fd SHA256 /s My /sha1 $Thumbprint $p.Sys
if ($LASTEXITCODE -ne 0) { throw 'SYS signing failed.' }
& (Join-Path $kits 'bin\10.0.28000.0\x86\Inf2Cat.exe') ('/driver:'+(Split-Path -Parent $p.Inf)) /os:10_CO_X64,10_NI_X64,10_GE_X64 /uselocaltime
if ($LASTEXITCODE -ne 0) { throw 'CAT regeneration failed.' }
& $sign sign /fd SHA256 /s My /sha1 $Thumbprint $p.Cat
if ($LASTEXITCODE -ne 0) { throw 'CAT signing failed.' }
Write-Host 'SYS/CAT signed locally. Trust and kernel loading policy require separate validation. Rebuilding requires signing again.'
