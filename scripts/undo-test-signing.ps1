# SPDX-License-Identifier: MIT
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RecordPath,[switch]$Execute)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'package-common.ps1')
if ((Get-Item -LiteralPath $RecordPath).Length -gt 16384) { throw 'Oversized test record.' }
$r=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $RecordPath).ProviderPath) | ConvertFrom-Json
if ($r.SchemaVersion -ne 1 -or $r.CertificateThumbprint -notmatch '^[A-Fa-f0-9]{40}$' -or
    $r.CertificateSubject -notmatch '^CN=OpenMagicKeyboard Local Test [a-f0-9]{32}$' -or
    $r.BootEntry -notmatch '^\{[0-9a-fA-F-]{36}\}$' -or $r.OriginalTestSigningElementAbsent -ne $true) { throw 'Invalid dedicated test-signing record.' }
Write-Host "PLAN: after project-driver removal, delete only the testsigning element on $($r.BootEntry), remove only certificate $($r.CertificateThumbprint) from Root/TrustedPublisher/CurrentUser My, then restart manually."
if (-not $Execute) { return }
Assert-Administrator
if ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value -ne $r.UserSid) { throw 'Use the same Windows user that created the test certificate.' }
$devices=@(Get-PnpDevice -PresentOnly -ErrorAction Stop | Where-Object { Test-ProjectInstance $_.InstanceId })
foreach ($device in $devices) {
    $provider=(Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName DEVPKEY_Device_DriverProvider -ErrorAction Stop).Data
    if ($provider -eq 'OpenMagicKeyboard Project') { throw 'Uninstall the project driver first; its target is still bound.' }
}
$bcd=Join-Path $env:WINDIR 'System32\bcdedit.exe'
$boot=& $bcd /enum $r.BootEntry /v
if ($LASTEXITCODE -ne 0) { throw 'Recorded boot entry unavailable; no settings changed.' }
if (($boot -join [Environment]::NewLine) -match '(?im)^\s*testsigning\s+') {
    & $bcd /deletevalue $r.BootEntry testsigning
    if ($LASTEXITCODE -ne 0) { throw 'Cannot restore original absent test-signing element. Certificates retained.' }
}
foreach ($store in @('LocalMachine\Root','LocalMachine\TrustedPublisher','CurrentUser\My')) {
    $path='Cert:\'+$store+'\'+$r.CertificateThumbprint
    if (Test-Path -LiteralPath $path) {
        if ((Get-Item -LiteralPath $path).Subject -ne $r.CertificateSubject) { throw 'Certificate identity mismatch; removal refused.' }
        if ($store -eq 'CurrentUser\My') { Remove-Item -LiteralPath $path -DeleteKey }
        else { Remove-Item -LiteralPath $path }
    }
}
Write-Host 'Original BCD element absence restored and dedicated test certificate removed. Restart manually and verify test signing is off.'
