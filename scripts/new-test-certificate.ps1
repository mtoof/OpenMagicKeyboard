# SPDX-License-Identifier: MIT
# Creates a local code-signing test certificate for this PC, trusts it (LocalMachine
# Root + TrustedPublisher) so Windows accepts the test-signed driver, and prints the
# thumbprint for sign-package.ps1. Plan only unless -Execute; trusting needs elevation.
[CmdletBinding()]
param([switch]$Execute,[int]$Months=12)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'package-common.ps1')
Write-Host "PLAN: create a non-exportable 'OpenMagicKeyboard Local Test' code-signing certificate valid for $Months months, trust it on this PC only, and print its thumbprint."
if (-not $Execute) { Write-Host 'PLAN ONLY. Rerun elevated with -Execute.'; return }
Assert-Administrator
$subject='CN=OpenMagicKeyboard Local Test '+[Guid]::NewGuid().ToString('N')
$cert=New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject -FriendlyName 'OpenMagicKeyboard local test' -CertStoreLocation 'Cert:\CurrentUser\My' `
    -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 -KeyExportPolicy NonExportable -NotAfter ([DateTime]::Now.AddMonths($Months))
$public=Join-Path ([IO.Path]::GetTempPath()) ($cert.Thumbprint+'.cer')
Export-Certificate -Cert $cert -FilePath $public -Type CERT | Out-Null
Import-Certificate -FilePath $public -CertStoreLocation 'Cert:\LocalMachine\Root' | Out-Null
Import-Certificate -FilePath $public -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher' | Out-Null
Remove-Item -LiteralPath $public
Write-Host "Thumbprint: $($cert.Thumbprint)"
Write-Host "Next: .\scripts\sign-package.ps1 -Thumbprint $($cert.Thumbprint) -Execute"
