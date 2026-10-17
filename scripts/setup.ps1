# One-step install / repair / uninstall for the OpenMagicKeyboard filter.
#   setup.ps1                      install or repair on every connected transport (USB + Bluetooth)
#   setup.ps1 -EnableTestSigning   also turn on Windows test signing when it is off (restart required)
#   setup.ps1 -Action Uninstall    restore Microsoft HID (or -RestoreVendor) and remove every project package
#   setup.ps1 -Action Status       read-only report
# Relaunches itself elevated when needed. Never reboots automatically.
[CmdletBinding()]
param([ValidateSet('Install','Uninstall','Status')][string]$Action='Install',
    [switch]$EnableTestSigning,[switch]$RestoreVendor,[switch]$NoPause,
    [ValidateSet('Release','Debug')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'package-common.ps1')
$Provider='OpenMagicKeyboard Project'
$Service='OpenMagicKeyboardFilter'
$TaskName='OpenMagicKeyboard Recovery'
$ProgramDataRoot=Join-Path $env:ProgramData 'OpenMagicKeyboard'
$pnputil=Join-Path $env:WINDIR 'System32\pnputil.exe'

function Test-Elevated {
    (New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
if ($Action -ne 'Status' -and -not (Test-Elevated)) {
    $arguments=@('-NoProfile','-ExecutionPolicy','RemoteSigned','-File',('"'+$PSCommandPath+'"'),'-Action',$Action,'-Configuration',$Configuration)
    if ($EnableTestSigning) { $arguments+='-EnableTestSigning' }
    if ($RestoreVendor) { $arguments+='-RestoreVendor' }
    if ($NoPause) { $arguments+='-NoPause' }
    $process=Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList $arguments -Verb RunAs -Wait -PassThru
    exit $process.ExitCode
}

function Get-Security { & (Join-Path $PSScriptRoot 'inspect-security.ps1') | ConvertFrom-Json }
function Get-Targets {
    @(Get-PnpDevice -PresentOnly -ErrorAction Stop | Where-Object { Test-ProjectInstance $_.InstanceId } | ForEach-Object {
        $id=$_.InstanceId
        $p=Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverInfPath,DEVPKEY_Device_DriverProvider,DEVPKEY_Device_ProblemCode,DEVPKEY_Device_DriverVersion -ErrorAction Stop
        [pscustomobject]@{
            InstanceId=$id
            Transport=$(if($id -like 'USB\*'){'USB'}else{'Bluetooth'})
            Inf=[string]($p | Where-Object KeyName -eq 'DEVPKEY_Device_DriverInfPath').Data
            Provider=[string]($p | Where-Object KeyName -eq 'DEVPKEY_Device_DriverProvider').Data
            Version=[string]($p | Where-Object KeyName -eq 'DEVPKEY_Device_DriverVersion').Data
            Problem=[int]($p | Where-Object KeyName -eq 'DEVPKEY_Device_ProblemCode').Data
        }
    })
}
function Get-ProjectPackages {
    @(Get-WindowsDriver -Online -ErrorAction Stop | Where-Object {
        $_.ProviderName -eq $Provider -and [IO.Path]::GetFileName($_.OriginalFileName) -ieq 'OpenMagicKeyboardFilter.inf'
    } | Sort-Object Driver -Unique)
}
function Test-LiveFilter([string]$InstanceId) {
    $stack=& $pnputil /enum-devices /instanceid $InstanceId /stack
    ($stack -join "`n") -match '(?im)^\s*(?:Stack:\s*)?(?:\\Driver\\)?OpenMagicKeyboardFilter\s*$'
}
function Show-Status {
    $s=Get-Security
    Write-Host ("Test signing active: {0}   Memory integrity: {1}   Secure Boot (registry): {2}" -f $s.TestSigningActive,$s.HvciKernelEnforcement,$s.SecureBootRegistryState)
    $targets=Get-Targets
    if (-not $targets) { Write-Host 'No Apple Magic Keyboard (05AC/004C PID 0320) is present.' }
    foreach ($t in $targets) {
        $live=if($t.Provider -eq $Provider){ if(Test-LiveFilter $t.InstanceId){'filter loaded'}else{'FILTER NOT LOADED'} }else{''}
        Write-Host ("{0,-9} {1} {2} ({3}) problem={4} {5}" -f $t.Transport,$t.Provider,$t.Version,$t.Inf,$t.Problem,$live)
    }
    if (Test-Elevated) {
        $packages=Get-ProjectPackages
        Write-Host ("Project packages staged: {0}" -f (($packages | ForEach-Object { "$($_.Driver) v$($_.Version)" }) -join ', '))
    } else { Write-Host 'Project packages staged: (run elevated to list)' }
    $svc=Get-ItemProperty -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Services\$Service" -ErrorAction SilentlyContinue
    if ($svc) {
        $image=[Environment]::ExpandEnvironmentVariables(($svc.ImagePath -replace '^\\SystemRoot',$env:WINDIR -replace '^\\\?\?\\',''))
        Write-Host ("Service image: {0} (exists: {1})" -f $svc.ImagePath,(Test-Path -LiteralPath $image))
    }
    Write-Host ("Recovery task registered: {0}" -f [bool](Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue))
}
function Invoke-Binding([string]$Verb,[string]$InstanceId,[string]$AuditRoot,[string]$Inf) {
    $binding=Join-Path $repoRoot "artifacts\$Configuration\MagicKeyboardDeviceBinding.exe"
    $audit=Join-Path $AuditRoot ('binding-'+[Guid]::NewGuid().ToString('N')+'.txt')
    $arguments=@($Verb,'--instance',$InstanceId)
    if ($Inf) { $arguments+=@('--inf',$Inf) }
    $arguments+=@('--audit',$audit)
    & $binding @arguments | Out-Host
    return $LASTEXITCODE
}
function Remove-StalePackages([string]$KeepInf) {
    foreach ($p in Get-ProjectPackages) {
        if ($KeepInf -and $p.Driver -ieq $KeepInf) { continue }
        Write-Host "Removing stale project package $($p.Driver) v$($p.Version)"
        & $pnputil /delete-driver $p.Driver | Out-Host
        if ($LASTEXITCODE -ne 0) { Write-Warning "$($p.Driver) is still in use and was kept." }
    }
}
function Register-Recovery {
    New-Item -ItemType Directory -Path $ProgramDataRoot -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot "artifacts\$Configuration\MagicKeyboardDeviceBinding.exe") -Destination $ProgramDataRoot -Force
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'recovery-watchdog.ps1') -Destination $ProgramDataRoot -Force
    # Only SYSTEM and Administrators may modify what the SYSTEM task executes.
    & icacls.exe $ProgramDataRoot /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' '*S-1-5-32-545:(OI)(CI)RX' | Out-Null
    $script=Join-Path $ProgramDataRoot 'recovery-watchdog.ps1'
    $action=New-ScheduledTaskAction -Execute (Join-Path $PSHOME 'powershell.exe') -Argument ('-NoProfile -ExecutionPolicy Bypass -File "'+$script+'"')
    $trigger=New-ScheduledTaskTrigger -AtStartup; $trigger.Delay='PT20S'
    $settings=New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Minutes 5)
    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Settings $settings -User 'SYSTEM' -RunLevel Highest -Force | Out-Null
    Write-Host "Recovery task registered: if the filter ever fails to load at boot (for example test signing was turned off), the keyboard is switched back to the Microsoft driver automatically."
}

$exitCode=0
try {
    if ($Action -eq 'Status') { Show-Status; return }
    $auditRoot=Join-Path $repoRoot ('observations\setup-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Path $auditRoot -Force | Out-Null
    Start-Transcript -LiteralPath (Join-Path $auditRoot 'transcript.txt') | Out-Null

    if ($Action -eq 'Uninstall') {
        foreach ($t in Get-Targets | Where-Object Provider -eq $Provider) {
            $verb=if($RestoreVendor){'--restore-vendor'}else{'--apply-microsoft'}
            Write-Host "Restoring $($t.Transport): $verb"
            $result=Invoke-Binding $verb $t.InstanceId $auditRoot $null
            if ($result -ne 0 -and $result -ne 3010) { throw "Could not rebind $($t.InstanceId) (exit $result)." }
        }
        Remove-StalePackages $null
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $ProgramDataRoot -Recurse -Force -ErrorAction SilentlyContinue
        Show-Status
        Write-Host 'Uninstalled. Test signing was left unchanged; scripts\undo-test-signing.ps1 reverts it.'
        return
    }

    # ---- Install / repair ----
    $security=Get-Security
    if (-not $security.TestSigningActive) {
        if (-not $EnableTestSigning) { throw 'Windows test signing is OFF, so this self-signed driver cannot load. Rerun with -EnableTestSigning, restart Windows, then run setup again.' }
        if ($security.SecureBootRegistryState -eq 1) { throw 'Secure Boot is enabled; Windows ignores test signing while it is on. Disable Secure Boot in firmware first.' }
        & (Join-Path $env:WINDIR 'System32\bcdedit.exe') /set '{current}' testsigning on | Out-Host
        if ($LASTEXITCODE -ne 0) { throw 'bcdedit could not enable test signing.' }
        Write-Host ''
        Write-Host 'Test signing is enabled for the NEXT boot. Restart Windows, then run setup again to install the driver.' -ForegroundColor Yellow
        Write-Host 'No driver was bound now, so your keyboard keeps working until then.'
        $exitCode=3010; return
    }
    $package=Read-ProjectPackage (Join-Path $repoRoot "artifacts\$Configuration\package")
    if ($package.SysSignature -ne 'Valid' -or $package.CatSignature -ne 'Valid') { throw 'Package SYS/CAT signatures are not trusted on this PC. Run sign-package.ps1 first.' }
    $hash=(Get-FileHash -LiteralPath $package.Sys -Algorithm SHA256).Hash
    $targets=Get-Targets
    if (-not $targets) { throw 'Connect the keyboard by USB or Bluetooth first.' }

    $stage=@(& $pnputil /add-driver $package.Inf)
    $stage | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'pnputil could not stage the package.' }
    $current=@(Get-ProjectPackages | Where-Object {
        $stored=Join-Path ([IO.Path]::GetDirectoryName($_.OriginalFileName)) 'OpenMagicKeyboardFilter.sys'
        (Test-Path -LiteralPath $stored) -and (Get-FileHash -LiteralPath $stored -Algorithm SHA256).Hash -eq $hash })
    if ($current.Count -ne 1) { throw 'Cannot uniquely identify the staged package.' }
    $currentInf=$current[0].Driver
    Write-Host "Staged as $currentInf"

    $failed=@()
    foreach ($t in $targets) {
        Write-Host "`n=== $($t.Transport): $($t.Provider) $($t.Version) -> $Provider" -ForegroundColor Cyan
        $result=Invoke-Binding '--apply-project' $t.InstanceId $auditRoot $package.Inf
        if ($result -eq 3010) { Write-Warning 'Windows requests a restart for this device.'; $exitCode=3010; continue }
        Start-Sleep -Seconds 3
        $after=Get-Targets | Where-Object InstanceId -eq $t.InstanceId
        if ($result -eq 0 -and $after -and $after.Problem -eq 0 -and (Test-LiveFilter $t.InstanceId)) {
            Write-Host "$($t.Transport): filter loaded, problem-free." -ForegroundColor Green
        } else {
            Write-Warning "$($t.Transport): filter did not start (exit $result, problem $($after.Problem)). Restoring the Microsoft driver so the keyboard keeps working."
            [void](Invoke-Binding '--apply-microsoft' $t.InstanceId $auditRoot $null)
            $failed+=$t.Transport
        }
    }
    # Root cause of the earlier failure: older project packages stayed staged and
    # Windows later picked one whose service binary had been deleted. Keep exactly one.
    Remove-StalePackages $currentInf
    Register-Recovery
    Write-Host ''
    Show-Status
    if ($failed) { throw ("Installation failed on: " + ($failed -join ', ') + ". See $auditRoot.") }
    Write-Host "`nInstalled. Type a few keys on the Apple keyboard, then open the Magic Keyboard app to apply mappings." -ForegroundColor Green
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    $exitCode=1
} finally {
    try { Stop-Transcript | Out-Null } catch { }
    if (-not $NoPause -and $Action -ne 'Status') { Write-Host ''; Read-Host 'Press Enter to close' | Out-Null }
    exit $exitCode  # in finally: early 'return' paths above still report their code
}
