// SPDX-License-Identifier: MIT. Bounded sideband protocol; no keyboard input reads.
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace OpenMagicKeyboard.Gui
{
    internal enum ApplyAction { ApplyProfile, DiagnoseFn, ConfirmFnProfile, WaitForFn }
    internal sealed class DriverState
    {
        internal bool LayoutValid, FnAvailable;
        internal uint ReportId, ReportBytes, FnDown, FnUp, ActiveFlags, PendingFlags;
        internal uint ProtocolVersion, Capabilities;
        internal bool MediaReady;
        internal byte ActiveFKeyMode, PendingFKeyMode;
        internal byte[] ActiveMap, PendingMap;
        internal string InstanceId;
    }
    internal static class DriverClient
    {
        private const uint StatusIoctl = 0x226000, ConfigIoctl = 0x22a004, DescriptorIoctl = 0x226008;
        private const string DeviceName = @"\\.\OpenMagicKeyboardControl";
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr security, uint disposition, uint attributes, IntPtr template);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool DeviceIoControl(SafeFileHandle handle, uint code, byte[] input, uint inBytes, byte[] output, uint outBytes, out uint returned, IntPtr overlapped);
        private static SafeFileHandle Open(bool write)
        {
            var handle = CreateFile(DeviceName, write ? 0xc0000000u : 0x80000000u, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
            if (handle.IsInvalid) { int error = Marshal.GetLastWin32Error(); handle.Dispose(); throw new Win32Exception(error); }
            return handle;
        }
        private static uint Number(byte[] data, int offset) { return BitConverter.ToUInt32(data, offset); }
        private static void Number(byte[] data, int offset, uint value) { Buffer.BlockCopy(BitConverter.GetBytes(value), 0, data, offset, 4); }
        internal static DriverState Decode(byte[] data)
        {
            if (data == null || data.Length < 8 || !((data.Length == 584 && Number(data, 4) == 1) || (data.Length == 600 && Number(data, 4) == 2)) || Number(data, 0) != data.Length)
                throw new InvalidDataException("Unsupported driver status protocol.");
            bool modern = Number(data, 4) == 2;
            int first = modern ? 40 : 32, size = modern ? 24 : 20, mapCount = modern ? 10 : 7, identityOffset = modern ? 88 : 72;
            for (int offset = first; offset <= first + size; offset += size) {
                if (Number(data, offset) != size || Number(data, offset + 4) != Number(data, 4) || (Number(data, offset + 8) & ~(modern ? 31u : 7u)) != 0 || data[offset + size - 1] != 0 || (modern && data[offset + 22] > 2))
                    throw new InvalidDataException("Invalid driver configuration.");
                for (int i = 0; i < mapCount; ++i) if (data[offset + 12 + i] > (modern ? 13 : 9)) throw new InvalidDataException("Unknown driver key action.");
            }
            string instance = Encoding.Unicode.GetString(data, identityOffset, 512);
            int end = instance.IndexOf('\0');
            if (end < 0) throw new InvalidDataException("Unterminated device identity.");
            instance = instance.Substring(0, end);
            if (!(instance.StartsWith(@"USB\VID_05AC&PID_0320&MI_01\", StringComparison.OrdinalIgnoreCase) ||
                instance.StartsWith(@"BTHENUM\{00001124-0000-1000-8000-00805f9b34fb}_VID&0001004c_PID&0320\", StringComparison.OrdinalIgnoreCase)) || instance.Length > 255 ||
                instance.IndexOf('"') >= 0 || instance.IndexOf('\r') >= 0 || instance.IndexOf('\n') >= 0)
                throw new InvalidDataException("Driver does not identify the supported Apple keyboard.");
            if (Number(data, 8) > 1 || Number(data, 20) > 1 || Number(data, 12) > 255 || Number(data, 16) > 65)
                throw new InvalidDataException("Invalid driver capabilities.");
            var state = new DriverState {
                LayoutValid = Number(data, 8) != 0, FnAvailable = Number(data, 20) != 0,
                ReportId = Number(data, 12), ReportBytes = Number(data, 16), FnDown = Number(data, 24), FnUp = Number(data, 28),
                ActiveFlags = Number(data, first + 8), PendingFlags = Number(data, first + size + 8), InstanceId = instance,
                ProtocolVersion = Number(data, 4), Capabilities = modern ? Number(data, 32) : 0, MediaReady = modern && Number(data, 36) != 0,
                ActiveFKeyMode = modern ? data[first + 22] : (byte)0, PendingFKeyMode = modern ? data[first + size + 22] : (byte)0,
                ActiveMap = new byte[10], PendingMap = new byte[10]
            };
            if (modern && ((state.Capabilities & ~63u) != 0 || Number(data, 36) > 1)) throw new InvalidDataException("Invalid driver capability flags.");
            Buffer.BlockCopy(data, first + 12, state.ActiveMap, 0, mapCount); Buffer.BlockCopy(data, first + size + 12, state.PendingMap, 0, mapCount); return state;
        }
        internal static SafeFileHandle OpenRead() { return Open(false); }
        internal static string Describe(DriverState state)
        {
            return "Device: " + state.InstanceId + "\nProtocol: " + state.ProtocolVersion + "; capabilities: " + state.Capabilities + "\nLayout: " + state.LayoutValid + " report " + state.ReportId + " / " + state.ReportBytes + " bytes\nActive flags: " + state.ActiveFlags + "; pending: " + state.PendingFlags + "\nFn available: " + state.FnAvailable + "; press/release: " + state.FnDown + "/" + state.FnUp + "\nF-key mode: " + state.ActiveFKeyMode + "; media helper: " + state.MediaReady;
        }
        internal static DriverState Read()
        {
            using (var handle = Open(false)) return Read(handle);
        }
        /// <summary>Status over an already open control handle (pollers reuse one handle).</summary>
        internal static DriverState Read(SafeFileHandle handle)
        {
            {
                var data = new byte[600]; uint returned;
                if (!DeviceIoControl(handle, StatusIoctl, null, 0, data, (uint)data.Length, out returned, IntPtr.Zero)) {
                    int error = Marshal.GetLastWin32Error(); if (error != 1 && error != 87 && error != 122) throw new Win32Exception(error);
                    data = new byte[584];
                    if (!DeviceIoControl(handle, StatusIoctl, null, 0, data, (uint)data.Length, out returned, IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
                }
                if (returned != data.Length) throw new InvalidDataException("Short driver status response.");
                return Decode(data);
            }
        }
        internal static byte[] Encode(MappingProfile profile, uint flags, uint version = 2)
        {
            profile.Validate(); if ((flags & ~7u) != 0 || (version != 1 && version != 2)) throw new InvalidDataException("Unknown driver flags or protocol.");
            bool extended = profile.FnCombinations || profile.SwapExchangedKeys || profile.FKeyMode != 0;
            for (int i = 0; i < 10; ++i) if ((i >= 7 && profile.Get(MappingProfile.Sources[i]) != KeyTarget.Original) || (byte)profile.Get(MappingProfile.Sources[i]) > 9) extended = true;
            if (version == 1 && extended) throw new InvalidDataException("These controls require the updated version 2 driver.");
            if ((profile.FnCombinations || profile.FKeyMode != 0) && (flags & 4) == 0) throw new InvalidDataException("Confirm physical Fn before enabling its combinations or F-key mode.");
            if (profile.FnCombinations) flags |= 8; if (profile.SwapExchangedKeys) flags |= 16;
            if ((flags & 1) != 0 && profile.Get("Fn") != KeyTarget.Original && (flags & 4) == 0)
                throw new InvalidDataException("First measure Fn in diagnostic mode and explicitly confirm its press/release behavior.");
            var bytes = new byte[version == 1 ? 20 : 24]; Number(bytes, 0, (uint)bytes.Length); Number(bytes, 4, version); Number(bytes, 8, flags);
            for (int i = 0; i < (version == 1 ? 7 : 10); ++i) bytes[12 + i] = (byte)profile.Get(MappingProfile.Sources[i]);
            if (version == 2) bytes[22] = (byte)profile.FKeyMode;
            return bytes;
        }
        internal static byte[] DecodeDescriptor(byte[] data)
        {
            if (data == null || data.Length != 4108 || Number(data, 0) != 4108 || (Number(data, 4) != 1 && Number(data, 4) != 2) ||
                Number(data, 8) == 0 || Number(data, 8) > 4096)
                throw new InvalidDataException("No bounded report descriptor is available from this driver.");
            var bytes = new byte[(int)Number(data, 8)]; Buffer.BlockCopy(data, 12, bytes, 0, bytes.Length); return bytes;
        }
        internal static void SaveDescriptor(string path)
        {
            Read(); // Validate the supported Apple keyboard identity before metadata export.
            byte[] bytes;
            using (var handle = Open(false)) {
                var data = new byte[4108]; uint returned;
                if (!DeviceIoControl(handle, DescriptorIoctl, null, 0, data, (uint)data.Length, out returned, IntPtr.Zero))
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                if (returned != data.Length) throw new InvalidDataException("Short descriptor response.");
                bytes = DecodeDescriptor(data);
            }
            using (var file = new FileStream(Path.GetFullPath(path), FileMode.CreateNew, FileAccess.Write, FileShare.None)) {
                file.Write(bytes, 0, bytes.Length); file.Flush(true);
            }
            Console.WriteLine("Saved " + bytes.Length + " descriptor bytes (device configuration only; no input reports).");
        }
        internal static void Apply(string path, bool diagnostic, bool confirmFn)
        {
            if (!new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator))
                throw new InvalidOperationException("Applying a device policy requires an elevated administrator token.");
            DriverState state = Read();
            MappingProfile profile = diagnostic ? MappingProfile.Defaults() : MappingProfile.Load(path);
            if (confirmFn && (!state.FnAvailable || state.FnDown == 0 || state.FnUp == 0 || (state.ActiveFlags & 2) == 0))
                throw new InvalidOperationException("No diagnostic Fn press/release pair is available to confirm.");
            uint flags = diagnostic ? 2u : 1u | (state.ActiveFlags & 4u) | (confirmFn ? 4u : 0u);
            // v0.4+ drivers accept the Fn field parsed from this keyboard's own descriptor.
            if (!diagnostic && DescriptorFn(state)) flags |= 4u;
            byte[] bytes = Encode(profile, flags, state.ProtocolVersion); uint returned;
            using (var handle = Open(true)) {
                if (!DeviceIoControl(handle, ConfigIoctl, bytes, (uint)bytes.Length, null, 0, out returned, IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
                if (returned != 0) throw new InvalidDataException("Unexpected configuration response.");
            }
            // The driver stores the policy for every transport; restart each one so
            // USB-C and Bluetooth both pick it up. The active transport must succeed.
            var targets = new System.Collections.Generic.List<string> { state.InstanceId };
            foreach (KeyboardNode node in DeviceStatus.Nodes())
                if (node.OurFilter && node.Problem == 0 && !String.Equals(node.InstanceId, state.InstanceId, StringComparison.OrdinalIgnoreCase)) targets.Add(node.InstanceId);
            for (int i = 0; i < targets.Count; ++i) {
                var start = new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "pnputil.exe"),
                    "/restart-device \"" + targets[i] + "\"") { UseShellExecute = false, CreateNoWindow = true };
                using (var process = Process.Start(start)) {
                    if (!process.WaitForExit(60000)) { if (i == 0) throw new IOException("Policy is saved, but the device restart is still running. Do not unplug during installation; check device status."); continue; }
                    if (process.ExitCode != 0 && i == 0) throw new IOException("Policy is saved but the keyboard restart failed (exit " + process.ExitCode + "). Reconnect or restart the target before retrying.");
                }
            }
        }
        internal static bool DescriptorFn(DriverState state) { return state.FnAvailable && (state.Capabilities & 32u) != 0; }
        internal static bool RequiresFn(MappingProfile profile)
        {
            profile.Validate();
            return profile.Get("Fn") != KeyTarget.Original || profile.FnCombinations || profile.FKeyMode != 0;
        }
        internal static ApplyAction SelectApplyAction(MappingProfile profile, DriverState state)
        {
            if (state == null) throw new ArgumentNullException("state");
            if (!state.LayoutValid) throw new InvalidOperationException("The keyboard report layout is unavailable. Input remains unchanged.");
            if (!RequiresFn(profile) || (state.ActiveFlags & 4) != 0 || DescriptorFn(state)) return ApplyAction.ApplyProfile;
            if ((state.ActiveFlags & 2) == 0) return ApplyAction.DiagnoseFn;
            return state.FnAvailable && state.FnDown >= 3 && state.FnDown == state.FnUp ? ApplyAction.ConfirmFnProfile : ApplyAction.WaitForFn;
        }
        internal static string DescribeError(Exception error)
        {
            if (error == null) throw new ArgumentNullException("error");
            var native = error as Win32Exception;
            if (native != null) {
                if (native.NativeErrorCode == 2 || native.NativeErrorCode == 1167)
                    return "The Apple keyboard driver is unavailable. Reconnect the keyboard using USB or Bluetooth, then retry Apply.";
                if (native.NativeErrorCode == 1223)
                    return "Windows administrator permission was cancelled. Click Apply and approve the Windows permission prompt to retry.";
            }
            return error.Message;
        }
        internal static void ApplyElevated(string path, ApplyAction action = ApplyAction.ApplyProfile)
        {
            if (String.IsNullOrWhiteSpace(path) || path.IndexOf('"') >= 0 || path.IndexOf('\r') >= 0 || path.IndexOf('\n') >= 0)
                throw new ArgumentException("Invalid profile path.");
            string arguments;
            switch (action) {
                case ApplyAction.ApplyProfile: arguments = "--apply-profile \"" + path + "\""; break;
                case ApplyAction.DiagnoseFn: arguments = "--diagnostic"; break;
                case ApplyAction.ConfirmFnProfile: arguments = "--confirm-fn-profile \"" + path + "\""; break;
                default: throw new ArgumentException("Unsupported elevated action.");
            }
            if (new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator)) {
                Apply(path, action == ApplyAction.DiagnoseFn, action == ApplyAction.ConfirmFnProfile);
                return;
            }
            var start = new ProcessStartInfo(System.Windows.Forms.Application.ExecutablePath, arguments) {
                UseShellExecute = true, Verb = "runas", WindowStyle = ProcessWindowStyle.Hidden
            };
            using (var process = Process.Start(start)) {
                if (!process.WaitForExit(65000)) throw new IOException("Apply is still running. Check driver status before retrying.");
                if (process.ExitCode != 0) throw new IOException("The driver policy could not be applied. Check the 'Driver policy failed' message from the administrator process.");
            }
        }
        internal static byte[] ReadMedia()
        {
            using (var handle = Open(false)) return ReadMedia(handle);
        }
        internal static byte[] ReadMedia(SafeFileHandle handle)
        {
            {
                var data = new byte[528]; uint returned;
                if (!DeviceIoControl(handle, 0x22600c, null, 0, data, (uint)data.Length, out returned, IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
                if (returned != 528 || Number(data, 0) != 528 || Number(data, 4) != 2 || Number(data, 12) > 64) throw new InvalidDataException("Invalid media action batch.");
                for (int i = 0; i < (int)Number(data, 12); ++i) {
                    int at = 16 + i * 8;
                    if (data[at + 4] < 1 || data[at + 4] > 12 || data[at + 5] > 1 || data[at + 6] != 0 || data[at + 7] != 0) throw new InvalidDataException("Invalid media action.");
                }
                return data;
            }
        }
        private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run", RunValue = "OpenMagicKeyboard media helper";
        /// <summary>Icon F-key modes need the per-user helper; keep it starting with Windows.</summary>
        internal static void SetHelperAutostart(bool enabled)
        {
            using (var key = Microsoft.Win32.Registry.CurrentUser.CreateSubKey(RunKey)) {
                if (enabled) key.SetValue(RunValue, "\"" + System.Windows.Forms.Application.ExecutablePath + "\" --media-helper");
                else if (key.GetValue(RunValue) != null) key.DeleteValue(RunValue);
            }
        }
        internal static bool HelperAutostart()
        {
            using (var key = Microsoft.Win32.Registry.CurrentUser.OpenSubKey(RunKey)) return key != null && key.GetValue(RunValue) != null;
        }
        internal static void StartMediaHelper()
        {
            Process.Start(new ProcessStartInfo(System.Windows.Forms.Application.ExecutablePath, "--media-helper") { UseShellExecute = true, WindowStyle = ProcessWindowStyle.Hidden });
        }
    }
}
