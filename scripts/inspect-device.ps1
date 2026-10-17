[CmdletBinding()]
param([string]$OutputPath)
$ErrorActionPreference = 'Stop'
Import-Module PnpDevice -ErrorAction Stop
# Metadata only: no input-report reads, device restart, registry writes, or installs.
$targets = @(Get-PnpDevice -PresentOnly -ErrorAction Stop | Where-Object {
    $_.InstanceId -match '^(USB|HID)\\VID_05AC&PID_0320(?:&|\\)' -or
    $_.InstanceId -match '^(BTHENUM|HID)\\\{00001124-0000-1000-8000-00805f9b34fb\}_VID&0001004c_PID&0320(?:&|\\)'
})
function Read-DeviceProperty([string]$InstanceId, [string]$Key) {
    $property = Get-PnpDeviceProperty -InstanceId $InstanceId -KeyName $Key -ErrorAction SilentlyContinue
    if ($null -ne $property) { return $property.Data }
    return $null
}
$records = @(foreach ($device in $targets) {
    [ordered]@{
        InstanceId = $device.InstanceId
        FriendlyName = $device.FriendlyName
        Class = $device.Class
        Status = [string]$device.Status
        HardwareIds = Read-DeviceProperty $device.InstanceId 'DEVPKEY_Device_HardwareIds'
        Parent = Read-DeviceProperty $device.InstanceId 'DEVPKEY_Device_Parent'
        DriverProvider = Read-DeviceProperty $device.InstanceId 'DEVPKEY_Device_DriverProvider'
        DriverInf = Read-DeviceProperty $device.InstanceId 'DEVPKEY_Device_DriverInfPath'
        DriverVersion = Read-DeviceProperty $device.InstanceId 'DEVPKEY_Device_DriverVersion'
        Service = Read-DeviceProperty $device.InstanceId 'DEVPKEY_Device_Service'
        ClassGuid = Read-DeviceProperty $device.InstanceId 'DEVPKEY_Device_ClassGuid'
        UpperFilters = Read-DeviceProperty $device.InstanceId 'DEVPKEY_Device_UpperFilters'
        LowerFilters = Read-DeviceProperty $device.InstanceId 'DEVPKEY_Device_LowerFilters'
        CompoundUpperFilters = Read-DeviceProperty $device.InstanceId 'DEVPKEY_Device_CompoundUpperFilters'
        CompoundLowerFilters = Read-DeviceProperty $device.InstanceId 'DEVPKEY_Device_CompoundLowerFilters'
        ProblemCode = Read-DeviceProperty $device.InstanceId 'DEVPKEY_Device_ProblemCode'
        DevNodeStatus = Read-DeviceProperty $device.InstanceId 'DEVPKEY_Device_DevNodeStatus'
    }
})
$classFilters = @(foreach ($classGuid in @($records.ClassGuid | Where-Object { $_ } | Sort-Object -Unique)) {
    $classPath = 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\' + $classGuid
    try {
        $values = Get-ItemProperty -LiteralPath $classPath -ErrorAction Stop
        [ordered]@{ ClassGuid = $classGuid; ReadSucceeded = $true; UpperFilters = $values.UpperFilters; LowerFilters = $values.LowerFilters }
    } catch {
        [ordered]@{ ClassGuid = $classGuid; ReadSucceeded = $false; Error = $_.Exception.Message }
    }
})
$inventory = [ordered]@{
    SchemaVersion = 1
    TimestampUtc = [DateTime]::UtcNow.ToString('o')
    Note = 'Metadata-only snapshot. Null device properties may be absent or inaccessible. Class filter reads never modify registry values. Re-run immediately before any future installation.'
    Devices = $records
    ClassFilters = $classFilters
}
$json = $inventory | ConvertTo-Json -Depth 10
if ($OutputPath) {
    $absolutePath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
    $stream = [IO.File]::Open($absolutePath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes($json + [Environment]::NewLine)
        $stream.Write($bytes, 0, $bytes.Length)
    } finally { $stream.Dispose() }
}
Write-Output $json
