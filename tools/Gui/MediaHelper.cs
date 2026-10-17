// SPDX-License-Identifier: MIT. Finite device-specific actions; no keyboard hook.
using System;
using System.Diagnostics;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;
namespace OpenMagicKeyboard.Gui
{
    internal sealed class MediaHelper : ApplicationContext
    {
        [StructLayout(LayoutKind.Sequential)] private struct KeyboardInput { internal ushort Key, Scan; internal uint Flags, Time; internal UIntPtr Extra; }
        [StructLayout(LayoutKind.Explicit, Size = 32)] private struct InputUnion { [FieldOffset(0)] internal KeyboardInput Keyboard; }
        [StructLayout(LayoutKind.Sequential)] private struct Input { internal uint Type; internal InputUnion Data; }
        [DllImport("user32.dll", SetLastError = true)] private static extern uint SendInput(uint count, Input[] input, int size);
        [DllImport("user32.dll")] private static extern short GetAsyncKeyState(int key);
        [DllImport("user32.dll", SetLastError = true)] private static extern IntPtr OpenInputDesktop(uint flags, bool inherit, uint access);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern bool GetUserObjectInformation(IntPtr handle, int index, StringBuilder text, uint length, out uint needed);
        [DllImport("user32.dll")] private static extern bool CloseDesktop(IntPtr handle);
        [DllImport("kernel32.dll")] private static extern uint WTSGetActiveConsoleSessionId();
        private readonly NotifyIcon tray = new NotifyIcon();
        private readonly System.Windows.Forms.Timer poll = new System.Windows.Forms.Timer();
        private readonly object brightnessLock = new object();
        private readonly Queue<int> brightnessQueue = new Queue<int>();
        private bool brightnessWorker;
        private volatile bool stopping;
        private string brightnessNotice;
        private volatile bool nativeBrightness;
        private Microsoft.Win32.SafeHandles.SafeFileHandle control; // reused; reopened after a disconnect
        private int ticks, idleChecks;
        internal MediaHelper()
        {
            if (Marshal.SizeOf(typeof(Input)) != 40) throw new InvalidOperationException("The media helper requires x64 Windows.");
            tray.Icon = System.Drawing.SystemIcons.Application; tray.Text = "Magic Keyboard media helper"; tray.Visible = true;
            var menu = new ContextMenuStrip(); menu.Items.Add("Exit media helper", null, delegate { ExitThread(); }); tray.ContextMenuStrip = menu;
            poll.Interval = 50; poll.Tick += delegate { Tick(); }; poll.Start();
        }
        internal static void Run()
        {
            bool created;
            using (var mutex = new Mutex(true, @"Local\OpenMagicKeyboard.MediaHelper", out created)) {
                if (!created) return;
                try {
                    DriverState state = DriverClient.Read();
                    if (state.ProtocolVersion != 2 || (state.Capabilities & 4) == 0) throw new InvalidOperationException("Install the version 2 driver before starting the media helper.");
                } catch (System.ComponentModel.Win32Exception error) {
                    if (error.NativeErrorCode != 2 && error.NativeErrorCode != 1167) throw;
                    // Wait for the supported USB keyboard to reconnect; never intercept other keyboards.
                }
                using (var context = new MediaHelper()) Application.Run(context);
            }
        }
        private static bool OnInputDesktop()
        {
            if ((uint)Process.GetCurrentProcess().SessionId != WTSGetActiveConsoleSessionId()) return false;
            IntPtr desktop = OpenInputDesktop(0, false, 1); if (desktop == IntPtr.Zero) return false;
            try { var name = new StringBuilder(256); uint needed; return GetUserObjectInformation(desktop, 2, name, 512, out needed) && name.ToString() == "Default"; }
            finally { CloseDesktop(desktop); }
        }
        private void Tick()
        {
            try {
                string notice;
                lock (brightnessLock) { notice = brightnessNotice; brightnessNotice = null; }
                if (notice != null) tray.ShowBalloonTip(3000, "Some displays could not change brightness", notice, ToolTipIcon.Info);
                if (!OnInputDesktop()) return;
                if (control == null) control = DriverClient.OpenRead();
                if (ticks++ % 40 == 0) { // status every 2 s; media drain every 50 ms
                    DriverState state = DriverClient.Read(control);
                    nativeBrightness = (state.Capabilities & 16) != 0;
                    // Nothing to do without an icon F-key mode: exit instead of polling forever.
                    if (state.ActiveFKeyMode == 0) { if (++idleChecks >= 3) { ExitThread(); return; } } else idleChecks = 0;
                }
                byte[] batch = DriverClient.ReadMedia(control);
                for (int i = 0; i < (int)BitConverter.ToUInt32(batch, 12); ++i) {
                    int action = batch[16 + i * 8 + 4] - 1; bool down = batch[16 + i * 8 + 5] != 0;
                    if (down) Perform(action + 1);
                }
            } catch { if (control != null) { control.Dispose(); control = null; } /* disconnect: reopen next tick */ }
        }
        private void Perform(int action)
        {
            if (action == 1 || action == 2) {
                lock (brightnessLock) {
                    if (stopping || brightnessQueue.Count >= 20) return;
                    brightnessQueue.Enqueue(action == 1 ? -1 : 1);
                    if (!brightnessWorker) { brightnessWorker = true; ThreadPool.QueueUserWorkItem(delegate { BrightnessLoop(); }); }
                }
                return;
            }
            switch (action) {
                case 3: Chord(0x5b, 0x09); break; // Task View
                case 4: Chord(0x5b, 0x53); break; // Search
                case 5: Chord(0x5b, 0x48); break; // Dictation
                case 6: Chord(0x5f); break; // Sleep, only on an explicit F6 action
                case 7: Chord(0xb1); break;
                case 8: Chord(0xb3); break;
                case 9: Chord(0xb0); break;
                case 10: Chord(0xad); break;
                case 11: Chord(0xae); break;
                case 12: Chord(0xaf); break;
            }
        }
        private static void Chord(params ushort[] keys)
        {
            var input = new Input[keys.Length * 2]; int count = 0;
            foreach (ushort key in keys) if ((GetAsyncKeyState(key) & 0x8000) == 0)
                input[count++] = new Input { Type = 1, Data = new InputUnion { Keyboard = new KeyboardInput { Key = key } } };
            int pressed = count;
            for (int i = pressed - 1; i >= 0; --i) { input[count] = input[i]; input[count++].Data.Keyboard.Flags = 2; }
            if (count != 0 && SendInput((uint)count, input, 40) != count) {
                var releases = new Input[pressed]; for (int i = 0; i < pressed; ++i) { releases[i] = input[i]; releases[i].Data.Keyboard.Flags = 2; }
                SendInput((uint)pressed, releases, 40);
            }
        }
        private void BrightnessLoop()
        {
            while (true) {
                int direction;
                lock (brightnessLock) {
                    if (stopping || brightnessQueue.Count == 0) { brightnessQueue.Clear(); brightnessWorker = false; return; }
                    direction = brightnessQueue.Dequeue();
                }
                if (!OnInputDesktop()) { lock (brightnessLock) brightnessQueue.Clear(); continue; }
                string notice = null;
                try { int failures; string result = nativeBrightness ? DisplayBrightness.RunExternal(direction, out failures) : DisplayBrightness.Run(direction, out failures); if (failures != 0) notice = result; }
                catch (Exception) { notice = "Brightness control failed. Check the external monitors' DDC/CI setting."; }
                if (notice != null) lock (brightnessLock) brightnessNotice = notice;
            }
        }
        protected override void Dispose(bool disposing) { if (disposing) { lock (brightnessLock) { stopping = true; brightnessQueue.Clear(); } poll.Dispose(); if (control != null) control.Dispose(); tray.Visible = false; tray.Dispose(); } base.Dispose(disposing); }
    }
}
