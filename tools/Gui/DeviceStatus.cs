using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace OpenMagicKeyboard.Gui
{
    internal enum DriverHealthKind { NotConnected, Active, NotInstalled, VendorDriver, CannotLoad, Failed }
    internal sealed class KeyboardNode
    {
        internal string InstanceId;
        internal bool Bluetooth;
        internal bool OurFilter, VendorFilter;
        internal bool Connected; // has live HID collections (a paired Bluetooth devnode stays present while idle)
        internal uint Problem;
    }
    internal sealed class DriverHealth
    {
        internal DriverHealthKind Kind;
        internal string Summary, Detail;
        internal List<KeyboardNode> Nodes = new List<KeyboardNode>();
        internal bool TestSigning;
    }

    // Configuration metadata only. No input handles, hooks, or driver writes.
    internal static class DeviceStatus
    {
        internal const string HardwareId = "USB\\VID_05AC&PID_0320&MI_01";
        internal const string BluetoothId = "BTHENUM\\{00001124-0000-1000-8000-00805f9b34fb}_VID&0001004c_PID&0320";
        private const uint SPDRP_HARDWAREID = 1, SPDRP_LOWERFILTERS = 0x12;
        [StructLayout(LayoutKind.Sequential)]
        private struct DeviceInfo { public uint Size; public Guid ClassGuid; public uint DevInst; public IntPtr Reserved; }
        [StructLayout(LayoutKind.Sequential)]
        private struct CodeIntegrityInfo { public uint Length, Options; }
        [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr SetupDiGetClassDevsW(ref Guid guid, string enumerator, IntPtr window, uint flags);
        [DllImport("setupapi.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetupDiEnumDeviceInfo(IntPtr set, uint index, ref DeviceInfo device);
        [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetupDiGetDeviceRegistryPropertyW(IntPtr set, ref DeviceInfo device,
            uint property, out uint type, byte[] buffer, uint size, out uint required);
        [DllImport("setupapi.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);
        [StructLayout(LayoutKind.Sequential)]
        private struct DevPropKey { public Guid Category; public uint Id; }
        // DEVPKEY_Device_DriverProvider. Declarative (INF AddFilter) filters do not
        // appear in the LowerFilters registry value, so identify the package instead.
        private static readonly DevPropKey DriverProvider = new DevPropKey { Category = new Guid("a8b865dd-2e3d-4094-ad97-e593a70c75d6"), Id = 9 };
        [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetupDiGetDevicePropertyW(IntPtr set, ref DeviceInfo device, ref DevPropKey key, out uint type, byte[] buffer, uint size, out uint required, uint flags);
        [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode)]
        private static extern uint CM_Get_Device_IDW(uint devInst, StringBuilder buffer, uint size, uint flags);
        [DllImport("cfgmgr32.dll")]
        private static extern uint CM_Get_Child(out uint child, uint devInst, uint flags);
        [DllImport("cfgmgr32.dll")]
        private static extern uint CM_Get_DevNode_Status(out uint status, out uint problem, uint devInst, uint flags);
        [DllImport("ntdll.dll")]
        private static extern int NtQuerySystemInformation(int kind, ref CodeIntegrityInfo info, int size, out int returned);

        private static string[] MultiString(IntPtr set, ref DeviceInfo info, uint property)
        {
            uint type, required;
            if (SetupDiGetDeviceRegistryPropertyW(set, ref info, property, out type, null, 0, out required) ||
                Marshal.GetLastWin32Error() != 122 || required < 2 || required > 65536 || required % 2 != 0) return new string[0];
            var bytes = new byte[required];
            if (!SetupDiGetDeviceRegistryPropertyW(set, ref info, property, out type, bytes, required, out required) ||
                type != 7 || required > bytes.Length) return new string[0];
            return Encoding.Unicode.GetString(bytes, 0, checked((int)required)).Split(new[] { '\0' }, StringSplitOptions.RemoveEmptyEntries);
        }
        /// <summary>Present Apple Magic Keyboard (PID 0320) USB MI_01 / Bluetooth HID devnodes.</summary>
        internal static List<KeyboardNode> Nodes()
        {
            var result = new List<KeyboardNode>();
            Guid hidClass = new Guid("745A17A0-74D3-11D0-B6FE-00A0C90F57DA");
            IntPtr set = SetupDiGetClassDevsW(ref hidClass, null, IntPtr.Zero, 2); // DIGCF_PRESENT
            if (set == new IntPtr(-1)) return result;
            try {
                for (uint i = 0; i < 4096; ++i) {
                    var info = new DeviceInfo { Size = checked((uint)Marshal.SizeOf(typeof(DeviceInfo))) };
                    if (!SetupDiEnumDeviceInfo(set, i, ref info)) break;
                    var instance = new StringBuilder(512);
                    if (CM_Get_Device_IDW(info.DevInst, instance, 512, 0) != 0) continue;
                    string id = instance.ToString();
                    bool usb = id.StartsWith(HardwareId + "\\", StringComparison.OrdinalIgnoreCase);
                    bool bt = id.StartsWith(BluetoothId + "\\", StringComparison.OrdinalIgnoreCase);
                    if (!usb && !bt) continue;
                    bool exact = false;
                    foreach (string hw in MultiString(set, ref info, SPDRP_HARDWAREID))
                        if (String.Equals(hw, usb ? HardwareId : BluetoothId, StringComparison.OrdinalIgnoreCase)) exact = true;
                    if (!exact) continue;
                    var node = new KeyboardNode { InstanceId = id, Bluetooth = bt };
                    string provider = Provider(set, ref info);
                    if (String.Equals(provider, "OpenMagicKeyboard Project", StringComparison.Ordinal)) node.OurFilter = true;
                    if (String.Equals(provider, "Magic Utilities Pty Ltd", StringComparison.Ordinal)) node.VendorFilter = true;
                    foreach (string filter in MultiString(set, ref info, SPDRP_LOWERFILTERS)) {
                        if (String.Equals(filter, "OpenMagicKeyboardFilter", StringComparison.OrdinalIgnoreCase)) node.OurFilter = true;
                        if (String.Equals(filter, "MagicKeyboard", StringComparison.OrdinalIgnoreCase)) node.VendorFilter = true;
                    }
                    uint status, problem;
                    if (CM_Get_DevNode_Status(out status, out problem, info.DevInst, 0) == 0) node.Problem = problem;
                    uint child; node.Connected = CM_Get_Child(out child, info.DevInst, 0) == 0;
                    result.Add(node);
                }
            } finally { SetupDiDestroyDeviceInfoList(set); }
            return result;
        }
        private static string Provider(IntPtr set, ref DeviceInfo info)
        {
            var key = DriverProvider; uint type, required; var buffer = new byte[512];
            if (!SetupDiGetDevicePropertyW(set, ref info, ref key, out type, buffer, (uint)buffer.Length, out required, 0) ||
                type != 0x12 || required < 2 || required > buffer.Length) return null; // DEVPROP_TYPE_STRING
            return Encoding.Unicode.GetString(buffer, 0, (int)required).TrimEnd((char)0);
        }
        internal static bool TestSigningActive()
        {
            var info = new CodeIntegrityInfo { Length = 8 }; int returned;
            return NtQuerySystemInformation(103, ref info, 8, out returned) == 0 && (info.Options & 2) != 0;
        }
        internal static string Read()
        {
            var nodes = Nodes();
            if (nodes.Count == 0) return "Apple keyboard not connected";
            return nodes.Exists(n => n.Connected) ? "Connected by " + Transports(nodes) : "Paired, not connected";
        }
        /// <summary>Explains why the control channel is or is not available, in user terms.</summary>
        internal static DriverHealth Diagnose(bool controlAvailable)
        {
            var health = new DriverHealth { Nodes = Nodes(), TestSigning = TestSigningActive() };
            var nodes = health.Nodes;
            if (nodes.Count == 0) {
                health.Kind = DriverHealthKind.NotConnected; health.Summary = "Keyboard not connected";
                health.Detail = "Pair the Magic Keyboard over Bluetooth or connect it with a USB-C data cable.";
                return health;
            }
            KeyboardNode broken = nodes.Find(n => n.OurFilter && n.Problem != 0);
            if (controlAvailable && broken == null) {
                health.Kind = DriverHealthKind.Active; health.Summary = "Driver active";
                health.Detail = "OpenMagicKeyboard driver is running on " + Transports(nodes.FindAll(n => n.OurFilter)) + ".";
                return health;
            }
            if (broken != null) {
                health.Kind = health.TestSigning ? DriverHealthKind.Failed : DriverHealthKind.CannotLoad;
                health.Summary = health.TestSigning ? "Driver failed to start" : "Driver blocked by Windows";
                health.Detail = health.TestSigning ? "The keyboard reports problem code " + broken.Problem + ". Click Repair driver to reinstall it."
                    : "Windows test signing is off, so the driver cannot load. Click Repair driver, then restart Windows.";
                return health;
            }
            if (nodes.Exists(n => n.VendorFilter)) {
                health.Kind = DriverHealthKind.VendorDriver; health.Summary = "Using the Magic Utilities driver";
                health.Detail = "Click Install driver to switch this keyboard to the OpenMagicKeyboard driver.";
                return health;
            }
            if (!nodes.Exists(n => n.OurFilter)) {
                health.Kind = DriverHealthKind.NotInstalled; health.Summary = "Driver not installed";
                health.Detail = "The keyboard uses the standard Windows driver (no Fn, media keys or remapping). Click Install driver.";
                return health;
            }
            health.Kind = DriverHealthKind.Failed; health.Summary = "Driver starting";
            health.Detail = "The driver is installed but not responding yet. Press a key on the Apple keyboard, or click Repair driver.";
            return health;
        }
        internal static string Transports(List<KeyboardNode> nodes)
        {
            List<KeyboardNode> live = nodes.FindAll(n => n.Connected);
            if (live.Count == 0) live = nodes; // driver lists: name every bound transport
            bool usb = live.Exists(n => !n.Bluetooth), bt = live.Exists(n => n.Bluetooth);
            return usb && bt ? "USB-C and Bluetooth" : usb ? "USB-C" : bt ? "Bluetooth" : "no connection";
        }
    }
}
