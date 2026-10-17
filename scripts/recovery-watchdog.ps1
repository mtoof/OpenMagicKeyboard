# SPDX-License-Identifier: MIT
# Boot-time safety net, installed by setup.ps1 as a SYSTEM scheduled task.
# A lower filter that cannot load (test signing turned off, image missing,
# Problem 39/52) takes the whole keyboard down with it. When that happens this
# switches only the affected Apple keyboard back to the Microsoft HID driver,
# so typing keeps working. It never touches healthy devices or other keyboards.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSCommandPath
$log=Join-Path $root 'recovery.log'
function Log([string]$text) { Add-Content -LiteralPath $log -Value ("{0:u} {1}" -f (Get-Date),$text) -Encoding UTF8 }
$usb='USB\VID_05AC&PID_0320&MI_01'
$bt='BTHENUM\{00001124-0000-1000-8000-00805f9b34fb}_VID&0001004c_PID&0320'
try {
    $targets=@(Get-PnpDevice -PresentOnly | Where-Object {
        $_.InstanceId -match ('^(?:'+[regex]::Escape($usb)+'|'+[regex]::Escape($bt)+')\\[^\\]+$') })
    foreach ($device in $targets) {
        $id=$device.InstanceId
        $provider=(Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverProvider).Data
        $problem=[int](Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_ProblemCode).Data
        if ($provider -ne 'OpenMagicKeyboard Project' -or $problem -eq 0) { continue }
        Log "$id has problem $problem with the project filter; restoring Microsoft HID."
        $audit=Join-Path $root ('recovery-'+[Guid]::NewGuid().ToString('N')+'.txt')
        & (Join-Path $root 'MagicKeyboardDeviceBinding.exe') --apply-microsoft --instance $id --audit $audit | Out-Null
        Log "Rebind exit code $LASTEXITCODE (audit $audit). Run scripts\setup.ps1 to reinstall once fixed."
    }
} catch { Log ("Watchdog error: " + $_.Exception.Message) }
