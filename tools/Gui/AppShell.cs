// SPDX-License-Identifier: MIT. Per-user app preferences, app icon, single instance.
using System;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using Microsoft.Win32;

namespace OpenMagicKeyboard.Gui
{
    /// <summary>Options menu settings. Only HKCU is touched, and only when the user toggles them.</summary>
    internal static class AppSettings
    {
        private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run", RunValue = "OpenMagicKeyboard";
        private const string SettingsKey = @"Software\OpenMagicKeyboard";
        internal const string StartupArgument = "--startup";

        internal static bool StartWithWindows
        {
            get { using (var key = Registry.CurrentUser.OpenSubKey(RunKey)) return key != null && key.GetValue(RunValue) != null; }
            set {
                using (var key = Registry.CurrentUser.CreateSubKey(RunKey)) {
                    if (value) key.SetValue(RunValue, "\"" + Application.ExecutablePath + "\" " + StartupArgument);
                    else if (key.GetValue(RunValue) != null) key.DeleteValue(RunValue);
                }
            }
        }
        internal static bool StartMinimized
        {
            get { using (var key = Registry.CurrentUser.OpenSubKey(SettingsKey)) return key != null && key.GetValue("StartMinimized") is int && (int)key.GetValue("StartMinimized") != 0; }
            set { using (var key = Registry.CurrentUser.CreateSubKey(SettingsKey)) key.SetValue("StartMinimized", value ? 1 : 0, RegistryValueKind.DWord); }
        }
    }

    internal static class SingleInstance
    {
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int RegisterWindowMessageW(string name);
        [DllImport("user32.dll")] private static extern bool PostMessageW(IntPtr window, int message, IntPtr wParam, IntPtr lParam);
        internal static readonly int ShowMessage = RegisterWindowMessageW("OpenMagicKeyboard.ShowWindow");
        /// <summary>Asks an already running app window to come to the front.</summary>
        internal static void ActivateExisting() { PostMessageW(new IntPtr(0xffff), ShowMessage, IntPtr.Zero, IntPtr.Zero); }
    }

    internal static class AppIcon
    {
        private static readonly System.Collections.Generic.Dictionary<int, Icon> cache = new System.Collections.Generic.Dictionary<int, Icon>();
        /// <summary>The embedded Magic Keyboard icon (tools\Gui\app.ico) at the requested size.</summary>
        internal static Icon Get(int size)
        {
            Icon icon;
            if (cache.TryGetValue(size, out icon)) return icon;
            using (var stream = typeof(AppIcon).Assembly.GetManifestResourceStream("OpenMagicKeyboard.app.ico"))
                icon = stream != null ? new Icon(stream, size, size) : SystemIcons.Application;
            cache[size] = icon; return icon;
        }
    }
}
