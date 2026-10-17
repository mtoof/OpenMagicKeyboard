# SPDX-License-Identifier: MIT
# Read-only signing prerequisites. Never retrieves BitLocker recovery passwords.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class OpenMagicSecurityState {
    [StructLayout(LayoutKind.Sequential)] public struct Info { public uint Length, Options; }
    [DllImport("ntdll.dll")] static extern int NtQuerySystemInformation(int kind, ref Info info, int size, out int returned);
    public static uint Options() { var info=new Info {Length=8}; int returned;
        if(NtQuerySystemInformation(103,ref info,8,out returned)!=0 || returned!=8) throw new InvalidOperationException("Code integrity state unavailable.");
        return info.Options;
    }
}
'@
$flags=[OpenMagicSecurityState]::Options()
$secureBoot=$null
try { $secureBoot=(Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\SecureBoot\State' -ErrorAction Stop).UEFISecureBootEnabled } catch { }
[pscustomobject]@{
    CodeIntegrityOptions=('0x{0:X8}' -f $flags)
    TestSigningActive=(($flags -band 2) -ne 0)
    HvciKernelEnforcement=(($flags -band 0x400) -ne 0)
    SecureBootRegistryState=$secureBoot
    Note='Read-only. Registry Secure Boot state is informational; firmware state and disk recovery readiness need explicit verification before an approved boot change.'
} | ConvertTo-Json
