// SPDX-License-Identifier: MIT. WMI laptop brightness and DDC/CI external brightness.
using System;
using System.Collections.Generic;
using System.Management;
using System.Runtime.InteropServices;

namespace OpenMagicKeyboard.Gui
{
    internal static class DisplayBrightness
    {
        [StructLayout(LayoutKind.Sequential)] private struct Rect { internal int Left, Top, Right, Bottom; }
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] private struct MonitorInfo {
            internal uint Size; internal Rect Monitor, Work; internal uint Flags;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] internal string Device;
        }
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] private struct DisplayDevice {
            internal uint Size;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] internal string Name;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] internal string Description;
            internal uint Flags;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] internal string Id;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] internal string Key;
        }
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] private struct PhysicalMonitor {
            internal IntPtr Handle;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] internal string Description;
        }
        private delegate bool MonitorCallback(IntPtr monitor, IntPtr dc, ref Rect rect, IntPtr data);
        [DllImport("user32.dll")] private static extern bool EnumDisplayMonitors(IntPtr dc, IntPtr rect, MonitorCallback callback, IntPtr data);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern bool GetMonitorInfo(IntPtr monitor, ref MonitorInfo info);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern bool EnumDisplayDevices(string device, uint index, ref DisplayDevice info, uint flags);
        [DllImport("dxva2.dll")] private static extern bool GetNumberOfPhysicalMonitorsFromHMONITOR(IntPtr monitor, out uint count);
        [DllImport("dxva2.dll", CharSet = CharSet.Unicode)] private static extern bool GetPhysicalMonitorsFromHMONITOR(IntPtr monitor, uint count, [Out] PhysicalMonitor[] physical);
        [DllImport("dxva2.dll")] private static extern bool DestroyPhysicalMonitors(uint count, [In] PhysicalMonitor[] physical);
        [DllImport("dxva2.dll")] private static extern bool GetMonitorBrightness(IntPtr monitor, out uint min, out uint current, out uint max);
        [DllImport("dxva2.dll")] private static extern bool SetMonitorBrightness(IntPtr monitor, uint value);
        [DllImport("dxva2.dll")] private static extern bool GetVCPFeatureAndVCPFeatureReply(IntPtr monitor, byte code, out uint type, out uint current, out uint max);
        [DllImport("dxva2.dll")] private static extern bool SetVCPFeature(IntPtr monitor, byte code, uint value);

        internal static uint Step(uint min, uint current, uint max, int direction)
        {
            if (max <= min || current < min || current > max || (direction != -1 && direction != 1))
                throw new ArgumentException("Invalid brightness range or direction.");
            long delta = Math.Max(1L, ((long)max - min + 19) / 20);
            return (uint)Math.Max(min, Math.Min(max, (long)current + direction * delta));
        }
        internal static string InterfaceIdentity(string id)
        {
            if (String.IsNullOrEmpty(id)) return "";
            if (id.StartsWith(@"\\?\", StringComparison.Ordinal)) id = id.Substring(4);
            int suffix = id.IndexOf("#{", StringComparison.Ordinal);
            if (suffix >= 0) id = id.Substring(0, suffix);
            return id.Replace('#', '\\');
        }
        private static bool UsesWmi(string instance, HashSet<string> wmi)
        {
            foreach (string name in wmi)
                if (name.StartsWith(instance + "_", StringComparison.OrdinalIgnoreCase) ||
                    String.Equals(name, instance, StringComparison.OrdinalIgnoreCase)) return true;
            return false;
        }
        // direction 0 is strictly read-only. Enumerate afresh to handle unplug/replug.
        internal static string Run(int direction, out int failures)
        { return RunCore(direction, out failures, false); }
        internal static string RunExternal(int direction, out int failures)
        { return RunCore(direction, out failures, true); }
        /// <summary>Sets every DDC/CI monitor to the same percentage as the built-in panel.</summary>
        internal static string SyncExternal(int percent, out int failures)
        {
            if (percent < 0 || percent > 100) throw new ArgumentException("Invalid brightness percentage.");
            return RunCore(1, out failures, true, percent);
        }
        /// <summary>Current built-in panel brightness (WMI), or -1 when there is none.</summary>
        internal static int InternalPercent()
        {
            try {
                using (var search = new ManagementObjectSearcher(@"root\wmi", "SELECT * FROM WmiMonitorBrightness"))
                using (var values = search.Get()) foreach (ManagementObject value in values) using (value)
                    if ((bool)value["Active"]) return Convert.ToInt32(value["CurrentBrightness"]);
            } catch (ManagementException) { }
            return -1;
        }
        private static string RunCore(int direction, out int failures, bool nativeLaptop, int absolutePercent = -1)
        {
            if (direction < -1 || direction > 1) throw new ArgumentException("Invalid direction.");
            var lines = new List<string>(); var wmi = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            int errors = 0;
            try {
                using (var search = new ManagementObjectSearcher(@"root\wmi", "SELECT * FROM WmiMonitorBrightness"))
                using (var values = search.Get()) foreach (ManagementObject value in values) using (value) {
                    if (!(bool)value["Active"]) continue;
                    string instance = (string)value["InstanceName"]; wmi.Add(instance);
                    int brightness = Convert.ToInt32(value["CurrentBrightness"]);
                    if (direction == 0 || nativeLaptop) { lines.Add(instance + ": " + brightness + "% (WMI)"); continue; }
                    byte target = (byte)Step(0, (uint)brightness, 100, direction);
                    // Match each methods object to its brightness instance; never broadcast one screen's value.
                    bool changed = false;
                    using (var methodSearch = new ManagementObjectSearcher(@"root\wmi", "SELECT * FROM WmiMonitorBrightnessMethods"))
                    using (var methods = methodSearch.Get()) foreach (ManagementObject method in methods) using (method) {
                        if (!(bool)method["Active"] || !String.Equals((string)method["InstanceName"], instance, StringComparison.OrdinalIgnoreCase)) continue;
                        using (var input = method.GetMethodParameters("WmiSetBrightness")) {
                            input["Timeout"] = (uint)0; input["Brightness"] = target;
                            using (var output = method.InvokeMethod("WmiSetBrightness", input, null))
                                changed = output == null || Convert.ToUInt32(output["ReturnValue"]) == 0;
                        }
                        break;
                    }
                    lines.Add(instance + (changed ? ": " + target + "% (WMI)" : ": brightness update failed"));
                    if (!changed) ++errors;
                }
            } catch (ManagementException) { /* External-only PCs may have no WMI brightness class. */ }
              catch (UnauthorizedAccessException) { lines.Add("WMI brightness access denied"); ++errors; }

            int logicalCount = 0;
            MonitorCallback callback = delegate(IntPtr monitor, IntPtr dc, ref Rect rect, IntPtr data) {
                if (++logicalCount > 32) return false;
                var info = new MonitorInfo { Size = (uint)Marshal.SizeOf(typeof(MonitorInfo)) };
                if (!GetMonitorInfo(monitor, ref info)) { ++errors; return true; }
                uint count;
                if (!GetNumberOfPhysicalMonitorsFromHMONITOR(monitor, out count) || count == 0 || count > 16) {
                    lines.Add(info.Device + ": no DDC/CI monitor access"); ++errors; return true;
                }
                for (uint i = 0; i < 16; ++i) {
                    var device = new DisplayDevice { Size = (uint)Marshal.SizeOf(typeof(DisplayDevice)) };
                    if (!EnumDisplayDevices(info.Device, i, ref device, 1)) break;
                    if ((device.Flags & 1) == 0) continue; // Ignore disconnected/inactive monitor registrations.
                    string identity = InterfaceIdentity(device.Id);
                    if (count == 1 && identity.Length != 0 && UsesWmi(identity, wmi)) return true;
                }
                var physical = new PhysicalMonitor[count];
                if (!GetPhysicalMonitorsFromHMONITOR(monitor, count, physical)) { ++errors; return true; }
                try {
                    foreach (var display in physical) {
                        uint min, current, max, type;
                        string name = info.Device + " / " + display.Description;
                        bool highLevel = GetMonitorBrightness(display.Handle, out min, out current, out max);
                        bool supported = highLevel;
                        if (!supported) { min = 0; supported = GetVCPFeatureAndVCPFeatureReply(display.Handle, 0x10, out type, out current, out max) && type == 1; }
                        if (!supported ||
                            max <= min || current < min || current > max) {
                            lines.Add(name + ": brightness unavailable (check DDC/CI in the monitor menu)"); ++errors; continue;
                        }
                        uint target = direction == 0 ? current : absolutePercent >= 0 ?
                            (uint)(min + Math.Round((max - min) * absolutePercent / 100.0)) : Step(min, current, max, direction);
                        if (absolutePercent >= 0 && target == current) { lines.Add(name + ": " + absolutePercent + "% (DDC/CI)"); continue; }
                        if (direction != 0 && !(highLevel ? SetMonitorBrightness(display.Handle, target) : SetVCPFeature(display.Handle, 0x10, target))) {
                            lines.Add(name + ": brightness update failed"); ++errors; continue;
                        }
                        lines.Add(name + ": " + Math.Round(100.0 * (target - min) / (max - min)) + "% (DDC/CI)");
                    }
                } finally { DestroyPhysicalMonitors(count, physical); }
                return true;
            };
            if (!EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, callback, IntPtr.Zero)) ++errors;
            failures = errors;
            if (lines.Count == 0) { ++failures; return "No supported brightness controls found."; }
            return String.Join(Environment.NewLine, lines.ToArray());
        }
    }
}
