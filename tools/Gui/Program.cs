// SPDX-License-Identifier: MIT
using System;
using System.IO;
using System.Runtime.Versioning;
using System.Text;
using System.Windows.Forms;

[assembly: TargetFramework(".NETFramework,Version=v4.8", FrameworkDisplayName = ".NET Framework 4.8")]

namespace OpenMagicKeyboard.Gui
{
    internal static class Program
    {
        [STAThread]
        private static int Main(string[] args)
        {
            try
            {
                if (args.Length == 1 && args[0] == "--self-test") return Checks.Run();
                if (args.Length == 1 && args[0] == "--media-helper") { Application.EnableVisualStyles(); MediaHelper.Run(); return 0; }
                if (args.Length == 1 && args[0] == "--brightness-status") { int failures; Console.WriteLine(DisplayBrightness.Run(0, out failures)); return failures == 0 ? 0 : 2; }
                if (args.Length == 2 && args[0] == "--test-f23") {
                    Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
                    using (var window = new F23TestWindow(args[1])) return window.ShowDialog() == DialogResult.OK ? 0 : 2;
                }
                if (args.Length == 2 && args[0] == "--test-fn") {
                    Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
                    using (var window = new FnTestWindow(args[1])) return window.ShowDialog() == DialogResult.OK ? 0 : 2;
                }
                if (args.Length == 2 && args[0] == "--descriptor") { DriverClient.SaveDescriptor(args[1]); return 0; }
                if (args.Length == 1 && args[0] == "--driver-status") {
                    Console.WriteLine(DriverClient.Describe(DriverClient.Read())); return 0;
                }
                if ((args.Length == 2 && args[0] == "--apply-profile") || (args.Length == 2 && args[0] == "--confirm-fn-profile") || (args.Length == 1 && args[0] == "--diagnostic")) {
                    try { DriverClient.Apply(args.Length == 2 ? args[1] : null, args[0] == "--diagnostic", args[0] == "--confirm-fn-profile"); return 0; }
                    catch (Exception error) { MessageBox.Show(error.Message, "Driver policy failed", MessageBoxButtons.OK, MessageBoxIcon.Error); return 1; }
                }
                if (args.Length == 1 && args[0] == "--help") {
                    Console.WriteLine("MagicKeyboardGui.exe [--profile FILE | --startup]\n--self-test\n--driver-status\n--test-fn NEW_FILE (interactive Fn-only counters)\n--test-f23 NEW_FILE (focused F23 only)\n--descriptor NEW_FILE (configuration only)\nAdministrator only: --apply-profile FILE | --diagnostic | --confirm-fn-profile FILE\nApply persists the policy for USB-C and Bluetooth and restarts those Apple keyboard devices only."); return 0;
                }
                string path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "OpenMagicKeyboard", "profile.json");
                bool startup = args.Length == 1 && args[0] == AppSettings.StartupArgument;
                if (args.Length != 0 && !startup) {
                    if (args.Length != 2 || args[0] != "--profile" || String.IsNullOrWhiteSpace(args[1]))
                        throw new ArgumentException("Use MagicKeyboardGui.exe [--profile FILE] or --self-test.");
                    path = args[1];
                }
                bool firstInstance;
                using (var instance = new System.Threading.Mutex(true, @"Local\OpenMagicKeyboard.App", out firstInstance)) {
                    // A second launch brings the running window forward (from the tray if hidden).
                    if (!firstInstance) { if (!startup) SingleInstance.ActivateExisting(); return 0; }
                    Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
                    Application.Run(new MainWindow(path, startup)); return 0;
                }
            }
            catch (Exception error) { Console.Error.WriteLine(error.Message); return 1; }
        }
    }
    internal static class Checks
    {
        private static int count;
        private static void Assert(bool condition, string name) { if (!condition) throw new InvalidOperationException("Check failed: " + name); ++count; }
        private static void Reject(Action action, string name)
        {
            bool rejected = false;
            try { action(); } catch (Exception error) {
                if (error is InvalidDataException || error is System.Runtime.Serialization.SerializationException || error is System.Xml.XmlException || error is ArgumentException) rejected = true;
                else throw;
            }
            Assert(rejected, name);
        }
        internal static int Run()
        {
            count = 0;
            Assert(DisplayBrightness.Step(0, 0, 100, -1) == 0 && DisplayBrightness.Step(0, 100, 100, 1) == 100, "brightness endpoints");
            Assert(DisplayBrightness.Step(20, 55, 120, -1) == 50 && DisplayBrightness.Step(20, 55, 120, 1) == 60, "brightness per-display range");
            Assert(DisplayBrightness.Step(0, UInt32.MaxValue, UInt32.MaxValue, 1) == UInt32.MaxValue, "brightness overflow bounds");
            var profile = MappingProfile.Defaults();
            var roundTrip = MappingProfile.Decode(profile.Encode());
            Assert(roundTrip.Get("Fn") == KeyTarget.Original && roundTrip.Mappings.Count == 10, "default round trip");
            var applyProfile = MappingProfile.Defaults();
            var applyState = new DriverState { LayoutValid = true, FnAvailable = true };
            Assert(DriverClient.SelectApplyAction(applyProfile, applyState) == ApplyAction.ApplyProfile, "original profile applies without Fn confirmation");
            applyProfile.Set("Fn", KeyTarget.LeftCtrl);
            Assert(DriverClient.SelectApplyAction(applyProfile, applyState) == ApplyAction.DiagnoseFn, "unverified Fn mapping starts diagnostic mode");
            applyState.ActiveFlags = 2;
            Assert(DriverClient.SelectApplyAction(applyProfile, applyState) == ApplyAction.WaitForFn, "empty diagnostic counters wait without elevation");
            applyState.FnDown = 3; applyState.FnUp = 2;
            Assert(DriverClient.SelectApplyAction(applyProfile, applyState) == ApplyAction.WaitForFn, "unreleased Fn waits without elevation");
            applyState.FnUp = 3;
            Assert(DriverClient.SelectApplyAction(applyProfile, applyState) == ApplyAction.ConfirmFnProfile, "three matched Fn taps select explicit confirmation");
            applyState.FnAvailable = false;
            Assert(DriverClient.SelectApplyAction(applyProfile, applyState) == ApplyAction.WaitForFn, "unavailable Fn cannot be confirmed from counters");
            applyState.FnAvailable = true; applyState.ActiveFlags = 4;
            Assert(DriverClient.SelectApplyAction(applyProfile, applyState) == ApplyAction.ApplyProfile, "verified Fn preserves ordinary apply");
            applyProfile = MappingProfile.Defaults(); applyProfile.FKeyMode = 2; applyState.ActiveFlags = 0;
            Assert(DriverClient.SelectApplyAction(applyProfile, applyState) == ApplyAction.DiagnoseFn, "icon F-key mode starts Fn diagnostics");
            applyProfile = MappingProfile.Defaults(); applyProfile.FnCombinations = true;
            Assert(DriverClient.SelectApplyAction(applyProfile, applyState) == ApplyAction.DiagnoseFn, "Fn combinations start Fn diagnostics");
            applyProfile = MappingProfile.Defaults(); applyProfile.Set("Fn", KeyTarget.F23);
            var descriptorState = new DriverState { LayoutValid = true, FnAvailable = true, Capabilities = 47 };
            Assert(DriverClient.SelectApplyAction(applyProfile, descriptorState) == ApplyAction.ApplyProfile, "v0.4 driver applies Fn without tap diagnostic");
            descriptorState.FnAvailable = false;
            Assert(DriverClient.SelectApplyAction(applyProfile, descriptorState) == ApplyAction.DiagnoseFn, "descriptor capability without Fn field still diagnoses");
            applyProfile = MappingProfile.Defaults(); applyState.ActiveFlags = 2;
            Assert(DriverClient.SelectApplyAction(applyProfile, applyState) == ApplyAction.ApplyProfile, "original profile exits diagnostics without confirming Fn");
            Reject(delegate { DriverClient.ApplyElevated("profile.json", ApplyAction.WaitForFn); }, "waiting action cannot launch an elevated process");
            Reject(delegate { DriverClient.ApplyElevated("profile.json", (ApplyAction)99); }, "unknown action cannot launch an elevated process");
            Reject(delegate { DriverClient.ApplyElevated("profile\r\n.json"); }, "elevated profile path rejects newlines before launch");
            string unavailable = DriverClient.DescribeError(new System.ComponentModel.Win32Exception(2));
            Assert(unavailable.Contains("Reconnect") && unavailable.Contains("USB or Bluetooth"), "missing control gives connection guidance");
            Assert(DriverClient.DescribeError(new System.ComponentModel.Win32Exception(1167)) == unavailable, "disconnected device gives connection guidance");
            string cancelled = DriverClient.DescribeError(new System.ComponentModel.Win32Exception(1223));
            Assert(cancelled.Contains("permission was cancelled") && cancelled != unavailable, "cancelled permission differs from disconnected device");
            Assert(DriverClient.DescribeError(new System.ComponentModel.Win32Exception(5, "Exact access error")) == "Exact access error", "other native errors retain precise detail");
            Assert(DriverClient.DescribeError(new IOException("Exact policy error")) == "Exact policy error", "policy errors retain precise detail");
            profile.SwapControlsAndWindows();
            Assert(profile.Get("LeftCtrl") == KeyTarget.LeftWindows && profile.Get("RightWindows") == KeyTarget.RightCtrl, "Ctrl/Windows side-aware swap");
            Assert(profile.Get("Fn") == KeyTarget.Original && profile.Get("LeftAlt") == KeyTarget.Original, "other keys preserved");
            profile.SwapControlsAndWindows(); Assert(profile.Get("LeftCtrl") == KeyTarget.Original && profile.Get("RightWindows") == KeyTarget.Original, "swap reversal");
            profile.SwapAltAndWindows(); Assert(profile.Get("LeftAlt") == KeyTarget.LeftWindows && profile.Get("RightWindows") == KeyTarget.RightAlt, "Alt/Windows side-aware swap");
            profile.Set("Fn", KeyTarget.F23); Assert(MappingProfile.Decode(profile.Encode()).Get("Fn") == KeyTarget.F23, "Fn/F23 profile round trip");
            foreach (KeyTarget target in Enum.GetValues(typeof(KeyTarget))) { profile.Set("Fn", target); Assert(MappingProfile.Decode(profile.Encode()).Get("Fn") == target, "target round trip " + target); }
            Reject(delegate { MappingProfile.Decode(new byte[MappingProfile.MaxBytes + 1]); }, "oversized profile");
            Reject(delegate { MappingProfile.Decode(Encoding.UTF8.GetBytes("{}")); }, "missing required fields");
            Reject(delegate { MappingProfile.Decode(Encoding.UTF8.GetBytes("not json")); }, "malformed JSON");
            var invalid = MappingProfile.Defaults(); invalid.Device.InterfaceNumber = 0; Reject(delegate { invalid.Validate(); }, "MI_00 rejected");
            invalid = MappingProfile.Defaults(); invalid.Device.ProductId = 0x0321; Reject(delegate { invalid.Validate(); }, "other product rejected");
            invalid = MappingProfile.Defaults(); invalid.SchemaVersion = 3; Reject(delegate { invalid.Validate(); }, "unknown version rejected");
            invalid = MappingProfile.Defaults(); invalid.Mappings["Fn"] = "1"; Reject(delegate { invalid.Validate(); }, "numeric enum rejected");
            invalid = MappingProfile.Defaults(); invalid.Mappings["Fn"] = "KernelMemoryWrite"; Reject(delegate { invalid.Validate(); }, "unknown action rejected");
            invalid = MappingProfile.Defaults(); invalid.Mappings.Remove("RightCtrl"); Reject(delegate { invalid.Validate(); }, "missing source rejected");
            invalid = MappingProfile.Defaults(); invalid.Mappings.Add("AllKeyboards", "LeftCtrl"); Reject(delegate { invalid.Validate(); }, "unknown source rejected");
            var config = DriverClient.Encode(MappingProfile.Defaults(), 1, 1);
            Assert(config.Length == 20 && config[0] == 20 && config[4] == 1 && config[8] == 1 && config[19] == 0, "fixed driver ABI");
            invalid = MappingProfile.Defaults(); invalid.Set("Fn", KeyTarget.F23);
            Reject(delegate { DriverClient.Encode(invalid, 1); }, "unverified Fn activation rejected");
            Assert(DriverClient.Encode(invalid, 5)[18] == 8, "verified Fn policy encoding");
            Reject(delegate { DriverClient.Encode(MappingProfile.Defaults(), 8); }, "unknown driver flags rejected");
            var status = new byte[584]; Buffer.BlockCopy(BitConverter.GetBytes(584), 0, status, 0, 4); status[4] = 1;
            Buffer.BlockCopy(config, 0, status, 32, 20); Buffer.BlockCopy(config, 0, status, 52, 20);
            byte[] identity = Encoding.Unicode.GetBytes(@"USB\VID_05AC&PID_0320&MI_01\SYNTHETIC");
            Buffer.BlockCopy(identity, 0, status, 72, identity.Length);
            Assert(DriverClient.Decode(status).InstanceId.EndsWith("SYNTHETIC", StringComparison.Ordinal), "bounded status decode");
            status[44] = 10; Reject(delegate { DriverClient.Decode(status); }, "unknown status action rejected"); status[44] = 0;
            status[8] = 2; Reject(delegate { DriverClient.Decode(status); }, "invalid capability boolean rejected"); status[8] = 0;
            Reject(delegate { DriverClient.Decode(new byte[583]); }, "short status rejected");
            status[72] = (byte)'H'; Reject(delegate { DriverClient.Decode(status); }, "wrong driver device identity rejected");
            var descriptor = new byte[4108]; Buffer.BlockCopy(BitConverter.GetBytes(4108), 0, descriptor, 0, 4); descriptor[4] = 1;
            descriptor[8] = 1; descriptor[12] = 0xc0;
            Assert(DriverClient.DecodeDescriptor(descriptor)[0] == 0xc0, "descriptor metadata decode");
            descriptor[8] = 0; Reject(delegate { DriverClient.DecodeDescriptor(descriptor); }, "unobserved descriptor rejected");
            Buffer.BlockCopy(BitConverter.GetBytes(4097), 0, descriptor, 8, 4);
            Reject(delegate { DriverClient.DecodeDescriptor(descriptor); }, "oversized descriptor rejected");
            Reject(delegate { DriverClient.DecodeDescriptor(new byte[4107]); }, "short descriptor protocol rejected");
            descriptor[4] = 3; Reject(delegate { DriverClient.DecodeDescriptor(descriptor); }, "descriptor version rejected");
            var extended = MappingProfile.Defaults(); extended.FnCombinations = true; extended.FKeyMode = 2; extended.Set("CapsLock", KeyTarget.LeftCtrl);
            byte[] modern = DriverClient.Encode(extended, 5);
            Assert(modern.Length == 24 && modern[4] == 2 && modern[8] == 13 && modern[19] == 1 && modern[22] == 2, "expanded fixed policy ABI");
            Reject(delegate { DriverClient.Encode(extended, 5, 1); }, "legacy driver refuses expanded controls");
            Reject(delegate { DriverClient.Encode(extended, 1); }, "Fn combinations require confirmation");
            var modernStatus = new byte[600]; Buffer.BlockCopy(BitConverter.GetBytes(600), 0, modernStatus, 0, 4); modernStatus[4] = 2;
            Buffer.BlockCopy(modern, 0, modernStatus, 40, 24); Buffer.BlockCopy(modern, 0, modernStatus, 64, 24); Buffer.BlockCopy(identity, 0, modernStatus, 88, identity.Length);
            Assert(DriverClient.Decode(modernStatus).ActiveMap[7] == 1 && DriverClient.Decode(modernStatus).ActiveFKeyMode == 2, "expanded status decode");
            Array.Clear(modernStatus, 88, 512);
            byte[] bluetoothIdentity = Encoding.Unicode.GetBytes(@"BTHENUM\{00001124-0000-1000-8000-00805f9b34fb}_VID&0001004c_PID&0320\TEST" + "\0");
            Buffer.BlockCopy(bluetoothIdentity, 0, modernStatus, 88, bluetoothIdentity.Length); modernStatus[32] = 31;
            Assert(DriverClient.Decode(modernStatus).Capabilities == 31, "Bluetooth identity and native brightness capability");
            modernStatus[32] = 63; Assert(DriverClient.Decode(modernStatus).Capabilities == 63, "descriptor Fn capability accepted");
            modernStatus[32] = 64; Reject(delegate { DriverClient.Decode(modernStatus); }, "unknown capability rejected");
            Console.WriteLine(count + " profile/protocol checks passed; no window, device, hook, or profile file opened."); return 0;
        }
    }
}
