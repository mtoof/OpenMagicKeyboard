// SPDX-License-Identifier: MIT. Reads only the Battery System (page 0x85) report
// of the Apple keyboard's non-keyboard HID collection. Never opens key input.
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace OpenMagicKeyboard.Gui
{
    internal sealed class BatteryState
    {
        internal int Percent = -1;    // -1: unavailable
        internal bool Charging, Discharging;
    }
    internal static class Battery
    {
        private const ushort BatteryPage = 0x85, StateOfCharge = 0x65, Charging = 0x44, Discharging = 0x46;
        [StructLayout(LayoutKind.Sequential)] private struct InterfaceData { public uint Size; public Guid ClassGuid; public uint Flags; public IntPtr Reserved; }
        [StructLayout(LayoutKind.Sequential)]
        private struct Caps {
            public ushort Usage, UsagePage, InputReportByteLength, OutputReportByteLength, FeatureReportByteLength;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)] public ushort[] Reserved;
            public ushort NumberLinkCollectionNodes, NumberInputButtonCaps, NumberInputValueCaps, NumberInputDataIndices,
                NumberOutputButtonCaps, NumberOutputValueCaps, NumberOutputDataIndices, NumberFeatureButtonCaps, NumberFeatureValueCaps, NumberFeatureDataIndices;
        }
        // HIDP_VALUE_CAPS is 72 bytes on x64; only the leading fields are needed.
        [StructLayout(LayoutKind.Explicit, Size = 72)]
        private struct ValueCaps {
            [FieldOffset(0)] public ushort UsagePage; [FieldOffset(2)] public byte ReportID;
            [FieldOffset(6)] public ushort LinkCollection; [FieldOffset(12)] public byte IsRange;
            [FieldOffset(56)] public ushort UsageMin; // union: NotRange.Usage
        }
        [DllImport("hid.dll")] private static extern void HidD_GetHidGuid(out Guid guid);
        [DllImport("hid.dll")] private static extern bool HidD_GetPreparsedData(SafeFileHandle handle, out IntPtr data);
        [DllImport("hid.dll")] private static extern bool HidD_FreePreparsedData(IntPtr data);
        [DllImport("hid.dll", SetLastError = true)] private static extern bool HidD_GetInputReport(SafeFileHandle handle, byte[] buffer, uint length);
        [DllImport("hid.dll")] private static extern int HidP_GetCaps(IntPtr data, ref Caps caps);
        [DllImport("hid.dll")] private static extern int HidP_GetValueCaps(int type, [Out] ValueCaps[] caps, ref ushort length, IntPtr data);
        [DllImport("hid.dll")] private static extern int HidP_GetUsageValue(int type, ushort page, ushort link, ushort usage, out uint value, IntPtr data, byte[] report, uint length);
        [DllImport("hid.dll")] private static extern int HidP_GetUsages(int type, ushort page, ushort link, [Out] ushort[] usages, ref uint length, IntPtr data, byte[] report, uint length2);
        [DllImport("setupapi.dll", SetLastError = true)] private static extern IntPtr SetupDiGetClassDevsW(ref Guid guid, IntPtr enumerator, IntPtr window, uint flags);
        [DllImport("setupapi.dll", SetLastError = true)] private static extern bool SetupDiEnumDeviceInterfaces(IntPtr set, IntPtr info, ref Guid guid, uint index, ref InterfaceData data);
        [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool SetupDiGetDeviceInterfaceDetailW(IntPtr set, ref InterfaceData data, IntPtr detail, uint size, out uint required, IntPtr info);
        [DllImport("setupapi.dll")] private static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr security, uint disposition, uint attributes, IntPtr template);

        internal static BatteryState Read()
        {
            var result = new BatteryState();
            Guid hid; HidD_GetHidGuid(out hid);
            IntPtr set = SetupDiGetClassDevsW(ref hid, IntPtr.Zero, IntPtr.Zero, 0x12); // PRESENT | DEVICEINTERFACE
            if (set == new IntPtr(-1)) return result;
            try {
                for (uint i = 0; i < 512; ++i) {
                    var data = new InterfaceData { Size = (uint)Marshal.SizeOf(typeof(InterfaceData)) };
                    if (!SetupDiEnumDeviceInterfaces(set, IntPtr.Zero, ref hid, i, ref data)) break;
                    string path = PathOf(set, ref data);
                    if (path == null) continue;
                    string lower = path.ToLowerInvariant();
                    if (!(lower.Contains("vid_05ac&pid_0320") || lower.Contains("vid&0001004c_pid&0320"))) continue;
                    if (TryRead(path, result)) return result;
                }
            } finally { SetupDiDestroyDeviceInfoList(set); }
            return result;
        }
        private static string PathOf(IntPtr set, ref InterfaceData data)
        {
            uint required;
            SetupDiGetDeviceInterfaceDetailW(set, ref data, IntPtr.Zero, 0, out required, IntPtr.Zero);
            if (required < 8 || required > 4096) return null;
            IntPtr buffer = Marshal.AllocHGlobal((int)required);
            try {
                Marshal.WriteInt32(buffer, 8); // cbSize of SP_DEVICE_INTERFACE_DETAIL_DATA_W on x64
                if (!SetupDiGetDeviceInterfaceDetailW(set, ref data, buffer, required, out required, IntPtr.Zero)) return null;
                return Marshal.PtrToStringUni(buffer + 4);
            } finally { Marshal.FreeHGlobal(buffer); }
        }
        private static bool TryRead(string path, BatteryState result)
        {
            // Keyboard collections are opened exclusively by Windows and fail here;
            // only the vendor/battery collection is readable, which is intended.
            using (SafeFileHandle handle = CreateFile(path, 0xC0000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero)) {
                if (handle.IsInvalid) return false;
                IntPtr ppd;
                if (!HidD_GetPreparsedData(handle, out ppd)) return false;
                try {
                    var caps = new Caps { Reserved = new ushort[17] };
                    if (HidP_GetCaps(ppd, ref caps) != 0x00110000 || caps.NumberInputValueCaps == 0 || caps.InputReportByteLength == 0) return false;
                    ushort count = caps.NumberInputValueCaps; var values = new ValueCaps[count];
                    if (HidP_GetValueCaps(0, values, ref count, ppd) != 0x00110000) return false;
                    for (int v = 0; v < count; ++v) {
                        if (values[v].UsagePage != BatteryPage || values[v].IsRange != 0 || values[v].UsageMin != StateOfCharge) continue;
                        var report = new byte[caps.InputReportByteLength]; report[0] = values[v].ReportID;
                        if (!HidD_GetInputReport(handle, report, (uint)report.Length)) return false;
                        uint level;
                        if (HidP_GetUsageValue(0, BatteryPage, values[v].LinkCollection, StateOfCharge, out level, ppd, report, (uint)report.Length) != 0x00110000) return false;
                        result.Percent = (int)Math.Min(100u, level);
                        var usages = new ushort[16]; uint length = 16;
                        if (HidP_GetUsages(0, BatteryPage, values[v].LinkCollection, usages, ref length, ppd, report, (uint)report.Length) == 0x00110000)
                            for (int u = 0; u < length; ++u) { if (usages[u] == Charging) result.Charging = true; if (usages[u] == Discharging) result.Discharging = true; }
                        return true;
                    }
                    return false;
                } finally { HidD_FreePreparsedData(ppd); }
            }
        }
    }
}
