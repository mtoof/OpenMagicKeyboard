# SPDX-License-Identifier: MIT
function Assert-Administrator {
    $p=New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Use Administrator PowerShell for execution.' }
}
function Read-StoredPackage([string]$PublishedInf) {
    if ($PublishedInf -notmatch '^oem[0-9]+\.inf$') { throw 'Invalid published package name.' }
    # The detailed -Driver query returns one record per model. Use package-level
    # inventory so a USB/Bluetooth package is still exactly one package.
    $packages=@(Get-WindowsDriver -Online -ErrorAction Stop | Where-Object { $_.Driver -ieq $PublishedInf })
    if ($packages.Count -ne 1) { throw 'Cannot uniquely identify stored package.' }
    return $packages[0]
}
function Test-ProjectInstance([string]$Instance) {
    $usb='USB\VID_05AC&PID_0320&MI_01'
    $bt='BTHENUM\{00001124-0000-1000-8000-00805f9b34fb}_VID&0001004c_PID&0320'
    return $Instance -match ('^(?:'+[regex]::Escape($usb)+'|'+[regex]::Escape($bt)+')\\[^\\]+$')
}
function Read-ProjectTarget([string]$RequestedInstance) {
    $devices=@(Get-PnpDevice -PresentOnly -ErrorAction Stop | Where-Object { Test-ProjectInstance $_.InstanceId })
    if ($RequestedInstance) { $devices=@($devices | Where-Object { $_.InstanceId -ieq $RequestedInstance }) }
    if ($devices.Count -ne 1) { throw 'Exactly one present MI_01 target is required. Supply an exact -InstanceId if needed.' }
    $id=$devices[0].InstanceId
    $ids=(Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_HardwareIds -ErrorAction Stop).Data
    $class=(Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_ClassGuid -ErrorAction Stop).Data
    $hardware=if($id -like 'USB\*'){'USB\VID_05AC&PID_0320&MI_01'}else{'BTHENUM\{00001124-0000-1000-8000-00805f9b34fb}_VID&0001004c_PID&0320'}
    if (@($ids) -notcontains $hardware -or [string]$class -ine '{745A17A0-74D3-11D0-B6FE-00A0C90F57DA}') { throw 'Target identity/HIDClass guard failed.' }
    [pscustomobject]@{
        InstanceId=$id
        BaselineInf=$(if($id -like 'USB\*'){'input.inf'}else{'hidbth.inf'})
        Inf=(Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverInfPath -ErrorAction Stop).Data
        Provider=(Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverProvider -ErrorAction Stop).Data
        Problem=(Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_ProblemCode -ErrorAction Stop).Data
    }
}
function Read-ProjectPackage([string]$Directory) {
    $root=(Resolve-Path -LiteralPath $Directory -ErrorAction Stop).ProviderPath
    $inf=Join-Path $root 'OpenMagicKeyboardFilter.inf'
    $sys=Join-Path $root 'OpenMagicKeyboardFilter.sys'
    $cat=Join-Path $root 'OpenMagicKeyboardFilter.cat'
    foreach ($file in @($inf,$sys,$cat)) { if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing package file: $file" } }
    if ((Get-Item -LiteralPath $inf).Length -gt 65536) { throw 'INF exceeds review bound.' }
    $text=[IO.File]::ReadAllText($inf)
    $models=[regex]::Match($text,'(?ms)^\[Models\.NTamd64[^\]]*\]\r?\n(.*?)(?=^\[|\z)').Groups[1].Value
    $modelLines=@($models -split '\r?\n' | ForEach-Object {$_.Trim()} | Where-Object {$_ -and -not $_.StartsWith(';')})
    $ids=@($modelLines | ForEach-Object { if($_ -notmatch '^%[^%]+%\s*=\s*Install(?:Bluetooth|Management)?\s*,\s*(.+?)\s*$'){throw 'Unexpected model line.'}; $Matches[1] })
    if ($ids.Count -ne 3 -or $ids -notcontains 'USB\VID_05AC&PID_0320&MI_00' -or $ids -notcontains 'USB\VID_05AC&PID_0320&MI_01' -or $ids -notcontains 'BTHENUM\{00001124-0000-1000-8000-00805f9b34fb}_VID&0001004c_PID&0320' -or
        $text -notmatch '(?im)^Provider="OpenMagicKeyboard Project"\s*$' -or
        $text -notmatch '(?im)^FilterPosition\s*=\s*Lower\s*$' -or
        $text -notmatch '(?im)^ServiceBinary\s*=\s*%13%\\OpenMagicKeyboardFilter\.sys\s*$') { throw 'Unexpected project INF identity or scope.' }
    [pscustomobject]@{ Inf=$inf; Sys=$sys; Cat=$cat
        SysSignature=[string](Get-AuthenticodeSignature -LiteralPath $sys).Status
        CatSignature=[string](Get-AuthenticodeSignature -LiteralPath $cat).Status
        Hashes=@($inf,$sys,$cat | ForEach-Object { Get-FileHash -LiteralPath $_ -Algorithm SHA256 })
    }
}
function Write-NewJson([string]$Path,$Value) {
    $b=[Text.UTF8Encoding]::new($false).GetBytes(($Value | ConvertTo-Json -Depth 10)+[Environment]::NewLine)
    $s=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $s.Write($b,0,$b.Length); $s.Flush($true) } finally { $s.Dispose() }
}
