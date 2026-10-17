// SPDX-License-Identifier: MIT
using System;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Windows.Forms;
namespace OpenMagicKeyboard.Gui {
/// <summary>White rounded card with a section title, matching Windows 11 settings panels.</summary>
internal sealed class Card : Panel {
    internal Card(string title) {
        Text = title; Dock = DockStyle.Fill; Margin = new Padding(6); Padding = new Padding(16, 40, 16, 12);
        BackColor = Color.FromArgb(243, 243, 243); DoubleBuffered = true;
        SetStyle(ControlStyles.ResizeRedraw, true);
    }
    protected override void OnPaint(PaintEventArgs e) {
        var g = e.Graphics; g.SmoothingMode = SmoothingMode.AntiAlias;
        var r = new Rectangle(0, 0, Width - 1, Height - 1);
        using (var path = new GraphicsPath()) {
            const int d = 14;
            path.AddArc(r.X, r.Y, d, d, 180, 90); path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90); path.AddArc(r.X, r.Bottom - d, d, d, 90, 90); path.CloseFigure();
            using (var fill = new SolidBrush(Color.White)) g.FillPath(fill, path);
            using (var pen = new Pen(Color.FromArgb(222, 222, 222))) g.DrawPath(pen, path);
        }
        using (var font = new Font("Segoe UI Semibold", 11.5F)) TextRenderer.DrawText(g, Text, font, new Point(14, 11), Color.FromArgb(28, 28, 28));
    }
    protected override void OnControlAdded(ControlEventArgs e) { base.OnControlAdded(e); if (e.Control.BackColor == Color.Transparent || e.Control is Label || e.Control is CheckBox) e.Control.BackColor = Color.White; }
}
/// <summary>Rounded battery gauge with the percentage centered.</summary>
internal sealed class BatteryBar : Control {
    private int percent = -1;
    internal BatteryBar() { SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw | ControlStyles.UserPaint, true); BackColor = Color.White; Height = 34; }
    internal int Percent { get { return percent; } set { percent = value; Invalidate(); } }
    protected override void OnPaint(PaintEventArgs e) {
        var g = e.Graphics; g.SmoothingMode = SmoothingMode.AntiAlias; g.Clear(BackColor);
        var r = new Rectangle(0, 0, Width - 1, Height - 1);
        using (var back = new SolidBrush(Color.FromArgb(240, 242, 245))) g.FillRectangle(back, r);
        if (percent >= 0) {
            Color fill = percent <= 15 ? Color.FromArgb(240, 128, 112) : Color.FromArgb(160, 196, 245);
            using (var brush = new SolidBrush(fill)) g.FillRectangle(brush, new Rectangle(r.X, r.Y, (int)(r.Width * percent / 100.0), r.Height));
        }
        using (var pen = new Pen(Color.FromArgb(205, 209, 214))) g.DrawRectangle(pen, r);
        using (var font = new Font("Segoe UI", 11F))
            TextRenderer.DrawText(g, percent >= 0 ? percent + "%" : "—", font, r, Color.FromArgb(32, 32, 32), TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
    }
}
internal sealed class MainWindow : Form {
    private MappingProfile profile;
    private readonly string profilePath;
    private readonly ComboBox[] selectors = new ComboBox[MappingProfile.Sources.Length];
    private readonly KeyboardView keyboard = new KeyboardView();
    private readonly BatteryBar battery = new BatteryBar();
    private readonly Label deviceTitle = new Label(), deviceSubtitle = new Label(), batteryText = new Label();
    private readonly Label driverTitle = new Label(), driverDetail = new Label(), saveStatus = new Label(), tab = new Label();
    private readonly LinkLabel layoutLink = new LinkLabel();
    private readonly Timer refresh = new Timer();
    private readonly ComboBox fkeyMode = new ComboBox();
    private readonly CheckBox combinations = new CheckBox(), exchanged = new CheckBox();
    private readonly ToolTip tips = new ToolTip();
    private Button apply, install;
    private bool loading, dirty, applying, readingBattery;
    private string applyError;
    private DateTime lastBattery = DateTime.MinValue;
    private bool lastBluetoothConnected;
    private Color dot = Color.Gray;
    // Natural output of each physical key, shown when a key keeps its original behavior.
    private static readonly string[] NaturalNames = { "ctrl", "right ctrl", "alt", "alt", "win", "win", "fn", "caps", "shift", "shift" };

    private NotifyIcon tray;
    private ToolStripMenuItem startWithWindows, startMinimized;
    private readonly bool startHidden;
    private bool allowShow;
    internal MainWindow(string path) : this(path, false) { }
    /// <param name="startup">Launched by Windows at sign-in (honors "Start minimized").</param>
    internal MainWindow(string path, bool startup) {
        startHidden = startup && AppSettings.StartMinimized;
        profilePath = Path.GetFullPath(path); profile = MappingProfile.Defaults();
        Text = "Magic Keyboard"; ClientSize = new Size(1240, 780); MinimumSize = new Size(1120, 740);
        StartPosition = FormStartPosition.CenterScreen; Font = new Font("Segoe UI", 10F); AutoScaleMode = AutoScaleMode.Dpi;
        BackColor = Color.FromArgb(243, 243, 243);
        Icon = AppIcon.Get(32);

        var root = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(12, 6, 12, 8), ColumnCount = 1, RowCount = 3 };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 44)); root.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); root.RowStyles.Add(new RowStyle(SizeType.Absolute, 26));
        Controls.Add(root);
        BuildMenuAndTray(); // added after the fill panel so the menu docks on top

        // Header: tab-style device selector (one keyboard; both transports share one profile).
        var header = new Panel { Dock = DockStyle.Fill, BackColor = BackColor };
        tab.AutoSize = false; tab.Size = new Size(250, 38); tab.Location = new Point(6, 6); tab.BackColor = Color.White;
        tab.Font = new Font("Segoe UI", 11F); tab.TextAlign = ContentAlignment.MiddleLeft; tab.Padding = new Padding(30, 0, 0, 0); tab.Text = "Magic Keyboard";
        tab.Paint += delegate(object s, PaintEventArgs e) {
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            using (var b = new SolidBrush(dot)) e.Graphics.FillEllipse(b, 10, 13, 13, 13);
            using (var p = new Pen(Color.FromArgb(222, 222, 222))) { e.Graphics.DrawLine(p, 0, 0, tab.Width - 1, 0); e.Graphics.DrawLine(p, 0, 0, 0, tab.Height); e.Graphics.DrawLine(p, tab.Width - 1, 0, tab.Width - 1, tab.Height); }
        };
        header.Controls.Add(tab);
        var model = new Label { Text = "Apple Magic Keyboard  ·  A3203 (USB-C, 2024)", AutoSize = true, ForeColor = Color.FromArgb(110, 110, 110), Location = new Point(270, 16) };
        header.Controls.Add(model);
        root.Controls.Add(header, 0, 0);

        var columns = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 1, Margin = new Padding(0) };
        columns.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 49)); columns.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 51));
        root.Controls.Add(columns, 0, 1);

        // ---- Left: device info ----
        var device = new Card("Device info");
        var deviceLayout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 6, BackColor = Color.White };
        deviceLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 30)); deviceLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 26));
        deviceLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); deviceLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
        deviceLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 46)); deviceLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));
        deviceTitle.Dock = DockStyle.Fill; deviceTitle.TextAlign = ContentAlignment.MiddleCenter; deviceTitle.Font = new Font("Segoe UI", 12F); deviceTitle.Text = "Magic Keyboard";
        deviceSubtitle.Dock = DockStyle.Fill; deviceSubtitle.TextAlign = ContentAlignment.MiddleCenter; deviceSubtitle.ForeColor = Color.FromArgb(120, 120, 120); deviceSubtitle.Font = new Font("Segoe UI", 11F);
        keyboard.Dock = DockStyle.Fill; keyboard.Margin = new Padding(0, 6, 0, 0);
        layoutLink.Dock = DockStyle.Fill; layoutLink.TextAlign = ContentAlignment.MiddleCenter; layoutLink.BackColor = Color.White;
        layoutLink.LinkClicked += delegate { profile.IsoLayout = !profile.IsoLayout; SetDirty(true); UpdatePreview(); };
        battery.Dock = DockStyle.Fill; battery.Margin = new Padding(0, 8, 0, 4);
        batteryText.Dock = DockStyle.Fill; batteryText.TextAlign = ContentAlignment.MiddleCenter;
        deviceLayout.Controls.Add(deviceTitle, 0, 0); deviceLayout.Controls.Add(deviceSubtitle, 0, 1); deviceLayout.Controls.Add(keyboard, 0, 2);
        deviceLayout.Controls.Add(layoutLink, 0, 3); deviceLayout.Controls.Add(battery, 0, 4); deviceLayout.Controls.Add(batteryText, 0, 5);
        device.Controls.Add(deviceLayout); columns.Controls.Add(device, 0, 0);

        // ---- Right: settings ----
        var right = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 4, Margin = new Padding(0) };
        right.RowStyles.Add(new RowStyle(SizeType.Absolute, 128)); right.RowStyles.Add(new RowStyle(SizeType.Absolute, 136));
        right.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); right.RowStyles.Add(new RowStyle(SizeType.Absolute, 176));
        columns.Controls.Add(right, 1, 0);

        var fkeys = new Card("F-Key Mappings");
        var fkeyLayout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 2, BackColor = Color.White };
        fkeyLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 30)); fkeyLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        fkeyLayout.Controls.Add(new Label { Text = "On F1 … F12 keys pressed:", AutoSize = true, Margin = new Padding(0, 0, 0, 6) }, 0, 0);
        fkeyMode.DropDownStyle = ComboBoxStyle.DropDownList; fkeyMode.Dock = DockStyle.Top; fkeyMode.Margin = new Padding(20, 0, 0, 0);
        fkeyMode.Items.AddRange(new object[] { "Standard F-keys (keyboard's original behavior)", "Use F1–F12; hold fn for icon functions", "Lock fn key = use icon functions (press fn + F-key for real F-key)" });
        fkeyMode.SelectedIndexChanged += delegate { if (!loading) { profile.FKeyMode = fkeyMode.SelectedIndex; SetDirty(true); UpdatePreview(); } };
        fkeyLayout.Controls.Add(fkeyMode, 0, 1); fkeys.Controls.Add(fkeyLayout); right.Controls.Add(fkeys, 0, 0);

        var other = new Card("Other Key Mappings");
        var otherLayout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 2, BackColor = Color.White };
        otherLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 42)); otherLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        otherLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); otherLayout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        exchanged.Text = "Swap exchanged keys (§ and `)"; exchanged.AutoSize = true;
        exchanged.CheckedChanged += delegate { if (!loading) { profile.SwapExchangedKeys = exchanged.Checked; if (exchanged.Checked) profile.IsoLayout = true; SetDirty(true); UpdatePreview(); } };
        combinations.Text = "fn + delete = Delete,  fn + arrows = Home / End / Page Up / Page Down"; combinations.AutoSize = true;
        combinations.CheckedChanged += delegate { if (!loading) { profile.FnCombinations = combinations.Checked; SetDirty(true); UpdatePreview(); } };
        tips.SetToolTip(exchanged, "For ISO (European) keyboards: swaps the § key left of 1 with the ` key left of Z.");
        otherLayout.Controls.Add(exchanged, 0, 0); otherLayout.Controls.Add(MakeButton("Show all key mappings", ShowMappings, false), 1, 0);
        otherLayout.Controls.Add(combinations, 0, 1); otherLayout.SetColumnSpan(combinations, 2);
        other.Controls.Add(otherLayout); right.Controls.Add(other, 0, 1);

        var modifiers = new Card("Modifier Key Mappings");
        var keys = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 7, RowCount = 5, BackColor = Color.White };
        float[] widths = { 12, 13, 13, 14, 20, 14, 14 };
        foreach (float w in widths) keys.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, w));
        for (int i = 0; i < 3; ++i) keys.RowStyles.Add(new RowStyle(SizeType.Absolute, 38));
        keys.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); keys.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
        AddSelector(keys, 7, 0, 0, 2, "Caps Lock");
        AddSelector(keys, 1, 5, 0, 2, "Right Control (not on the compact Magic Keyboard; external keys only)");
        AddSelector(keys, 8, 0, 1, 2, "Left Shift");
        AddSelector(keys, 9, 5, 1, 2, "Right Shift");
        int[] physical = { 6, 0, 2, 4, -1, 5, 3 }; string[] names = { "fn / Globe", "Left Control", "Left Option", "Left Command", "", "Right Command", "Right Option" };
        for (int i = 0; i < 7; ++i) {
            if (physical[i] < 0) { keys.Controls.Add(new Label { Text = "space", Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleCenter, ForeColor = Color.FromArgb(140, 140, 140), BackColor = Color.FromArgb(240, 240, 240), Margin = new Padding(3, 4, 3, 4) }, i, 2); continue; }
            AddSelector(keys, physical[i], i, 2, 1, names[i]);
        }
        var quick = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false, BackColor = Color.White, Margin = new Padding(0) };
        quick.Controls.Add(MakeButton("Swap ctrl ⇄ cmd", delegate { profile.SwapControlsAndWindows(); LoadSelectors(); SetDirty(true); }, false));
        quick.Controls.Add(MakeButton("Swap alt ⇄ cmd", delegate { profile.SwapAltAndWindows(); LoadSelectors(); SetDirty(true); }, false));
        quick.Controls.Add(MakeButton("Reset all", delegate { bool iso = profile.IsoLayout; profile = MappingProfile.Defaults(); profile.IsoLayout = iso; LoadSelectors(); SetDirty(true); }, false));
        keys.Controls.Add(quick, 0, 4); keys.SetColumnSpan(quick, 7);
        modifiers.Controls.Add(keys); right.Controls.Add(modifiers, 0, 2);

        var driver = new Card("Driver");
        var driverLayout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 3, BackColor = Color.White };
        driverLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); driverLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 140));
        driverLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 26)); driverLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); driverLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 40));
        driverTitle.Dock = DockStyle.Fill; driverTitle.Font = new Font("Segoe UI Semibold", 10.5F); driverTitle.TextAlign = ContentAlignment.MiddleLeft;
        driverDetail.Dock = DockStyle.Fill; driverDetail.ForeColor = Color.FromArgb(90, 90, 90); driverDetail.Margin = new Padding(3, 2, 12, 2);
        install = MakeButton("Install driver", InstallDriver, false); install.Visible = false;
        apply = new Button { Text = "\r\nApply to keyboard", Dock = DockStyle.Fill, FlatStyle = FlatStyle.Flat, BackColor = Color.FromArgb(229, 241, 251), ForeColor = Color.FromArgb(20, 20, 20), Margin = new Padding(8, 0, 0, 0), Enabled = false };
        apply.FlatAppearance.BorderColor = Color.FromArgb(160, 200, 240);
        apply.Paint += delegate(object s, PaintEventArgs e) {
            using (var glyph = new Font("Segoe MDL2 Assets", 22F))
                TextRenderer.DrawText(e.Graphics, "", glyph, new Rectangle(0, 12, apply.Width, 34), apply.Enabled ? Color.FromArgb(0, 103, 192) : Color.Gray, TextFormatFlags.HorizontalCenter);
        };
        apply.Click += delegate { Apply(); };
        driverLayout.Controls.Add(driverTitle, 0, 0); driverLayout.Controls.Add(driverDetail, 0, 1);
        var driverButtons = new FlowLayoutPanel { Dock = DockStyle.Fill, BackColor = Color.White, Margin = new Padding(0), WrapContents = false };
        driverButtons.Controls.Add(install); driverLayout.Controls.Add(driverButtons, 0, 2);
        driverLayout.Controls.Add(apply, 1, 0); driverLayout.SetRowSpan(apply, 3);
        driver.Controls.Add(driverLayout); right.Controls.Add(driver, 0, 3);

        saveStatus.Dock = DockStyle.Fill; saveStatus.AutoEllipsis = true; saveStatus.ForeColor = Color.FromArgb(110, 110, 110); saveStatus.TextAlign = ContentAlignment.MiddleLeft;
        root.Controls.Add(saveStatus, 0, 2);

        LoadSelectors(); SetDirty(false);
        if (File.Exists(profilePath)) { try { profile = MappingProfile.Load(profilePath); LoadSelectors(); SetDirty(false); } catch (Exception e) { MessageBox.Show(e.Message, "Invalid profile"); } }
        // Poll only while the window is on screen; hidden in the tray the app is idle.
        refresh.Interval = 4000; refresh.Tick += delegate { if (Visible && WindowState != FormWindowState.Minimized) RefreshDevice(); }; refresh.Start();
        Shown += delegate { RefreshDevice(); };
        FormClosing += delegate(object sender, FormClosingEventArgs e) {
            if (applying) { e.Cancel = true; return; }
            if (!dirty) return;
            var answer = MessageBox.Show(this, "Save your changes before closing?", "Unsaved changes", MessageBoxButtons.YesNoCancel);
            if (answer == DialogResult.Cancel || (answer == DialogResult.Yes && !Save())) e.Cancel = true;
        };
    }
    private void AddSelector(TableLayoutPanel table, int index, int column, int row, int span, string physicalName) {
        var selector = new ComboBox { Dock = DockStyle.Fill, DropDownStyle = ComboBoxStyle.DropDownList, DropDownWidth = 140, Margin = new Padding(3, 5, 3, 3), AccessibleName = "Mapping for " + physicalName };
        for (int t = 0; t < MappingProfile.ShortLabels.Length; ++t) selector.Items.Add(t == 0 ? NaturalNames[index] : MappingProfile.ShortLabels[t]);
        selectors[index] = selector; tips.SetToolTip(selector, physicalName + " key");
        selector.SelectedIndexChanged += delegate { if (!loading && selector.SelectedIndex >= 0) { profile.Set(MappingProfile.Sources[index], (KeyTarget)selector.SelectedIndex); SetDirty(true); UpdatePreview(); } };
        if (span == 2) { selector.Dock = DockStyle.None; selector.Width = 120; selector.Anchor = column == 0 ? AnchorStyles.Left : AnchorStyles.Right; }
        table.Controls.Add(selector, column, row); table.SetColumnSpan(selector, span);
    }
    private Button MakeButton(string text, Action action, bool fill) {
        var b = new Button { Text = text, AutoSize = true, Height = 32, Padding = new Padding(8, 0, 8, 0), Margin = new Padding(0, 3, 8, 3), FlatStyle = FlatStyle.System };
        if (fill) b.Dock = DockStyle.Fill;
        b.Click += delegate { action(); }; return b;
    }
    private void LoadSelectors() {
        loading = true;
        try {
            for (int i = 0; i < selectors.Length; ++i) selectors[i].SelectedIndex = (int)profile.Get(MappingProfile.Sources[i]);
            fkeyMode.SelectedIndex = profile.FKeyMode; combinations.Checked = profile.FnCombinations; exchanged.Checked = profile.SwapExchangedKeys;
        } finally { loading = false; }
        UpdatePreview();
    }
    private void UpdatePreview() {
        keyboard.Show(profile);
        layoutLink.Text = profile.IsoLayout ? "ISO layout (European)  ·  switch to ANSI" : "ANSI layout (US)  ·  switch to ISO";
        layoutLink.LinkArea = new LinkArea(layoutLink.Text.IndexOf("switch", StringComparison.Ordinal), layoutLink.Text.Length - layoutLink.Text.IndexOf("switch", StringComparison.Ordinal));
    }
    private void SetDirty(bool value) {
        dirty = value; Text = "Magic Keyboard" + (dirty ? " *" : "");
        saveStatus.Text = (dirty ? "Unsaved changes — Apply saves and activates them." : "Profile: " + profilePath);
    }
    private bool Save() { try { profile.Save(profilePath); SetDirty(false); return true; } catch (Exception e) { MessageBox.Show(this, e.Message, "Save failed"); return false; } }

    private void RefreshDevice() {
        DriverState state = null; Exception controlError = null;
        try { state = DriverClient.Read(); } catch (Exception e) { controlError = e; }
        DriverHealth health;
        try { health = DeviceStatus.Diagnose(state != null && state.LayoutValid); } catch { health = new DriverHealth { Kind = DriverHealthKind.Failed, Summary = "Status unavailable", Detail = "" }; }
        deviceSubtitle.Text = health.Nodes.Count == 0 ? "Not connected" : health.Nodes.Exists(n => n.Connected) ? "Connected by " + DeviceStatus.Transports(health.Nodes) : "Paired, not connected";
        dot = health.Kind == DriverHealthKind.Active ? Color.FromArgb(46, 160, 67) : health.Kind == DriverHealthKind.NotConnected ? Color.FromArgb(160, 160, 160) : Color.FromArgb(230, 160, 30);
        tab.Invalidate();
        driverTitle.Text = health.Summary; driverDetail.Text = health.Detail;
        install.Visible = health.Kind != DriverHealthKind.Active && health.Kind != DriverHealthKind.NotConnected;
        install.Text = health.Kind == DriverHealthKind.CannotLoad || health.Kind == DriverHealthKind.Failed ? "Repair driver" : "Install driver";
        apply.Enabled = state != null && state.LayoutValid && !applying;
        if (state != null && state.LayoutValid) {
            string extra = null;
            if ((state.ActiveFlags & 2) != 0) extra = FnDiagnosticGuidance(state);
            else if (DriverClient.RequiresFn(profile) && (state.ActiveFlags & 4) == 0 && !DriverClient.DescriptorFn(state))
                extra = "This driver build needs a one-time fn check: click Apply, tap fn three times, then Apply again.";
            else if (state.ActiveFKeyMode != 0 && !state.MediaReady) extra = "Icon functions start with Windows; the media helper is starting.";
            driverDetail.Text = health.Detail + (extra != null ? "\r\n" + extra : "\r\nChoose your mappings, then click Apply.");
        } else if (state != null && health.Kind == DriverHealthKind.Active) {
            driverDetail.Text = "Driver loaded; waiting for the keyboard's report layout. Press any key on the Apple keyboard.";
        } else if (controlError != null && health.Kind == DriverHealthKind.Failed && health.Nodes.Exists(n => n.OurFilter)) {
            driverDetail.Text = DriverClient.DescribeError(controlError);
        }
        if (applyError != null) driverDetail.Text = applyError;
        // Reading the battery sends a GET_REPORT over the Bluetooth radio, which the
        // keyboard shares with Bluetooth mice. Ask rarely: on connect, then every 10 min.
        bool bluetooth = health.Nodes.Exists(n => n.Bluetooth && n.Connected);
        if (!readingBattery && (bluetooth != lastBluetoothConnected || (DateTime.UtcNow - lastBattery).TotalMinutes >= 10)) {
            lastBluetoothConnected = bluetooth; RefreshBattery(health);
        }
    }
    private async void RefreshBattery(DriverHealth health) {
        readingBattery = true; lastBattery = DateTime.UtcNow;
        BatteryState b = null;
        try { b = await System.Threading.Tasks.Task.Run(() => Battery.Read()); } catch { }
        readingBattery = false;
        if (IsDisposed) return;
        battery.Percent = b != null ? b.Percent : -1;
        if (b != null && b.Percent >= 0) batteryText.Text = b.Charging ? "Battery is charging." : b.Discharging ? "Battery is discharging." : "Battery level";
        else batteryText.Text = health.Nodes.Count == 0 ? "" : health.Nodes.Exists(n => n.Bluetooth && n.Connected) ? "Battery level unavailable right now." : "Battery level is shown while the keyboard uses Bluetooth (cable unplugged).";
    }
    private static string FnDiagnosticGuidance(DriverState state) {
        string counts = "fn press/release: " + state.FnDown + " / " + state.FnUp + ". ";
        return state.FnAvailable && state.FnDown >= 3 && state.FnDown == state.FnUp ? counts + "fn detected — click Apply to finish." :
            counts + "Release all keys, tap fn / Globe three times, then click Apply.";
    }
    private async void Apply() {
        if (applying) return;
        applyError = null;
        if (!Save()) return;
        try {
            DriverState state = DriverClient.Read();
            ApplyAction action = DriverClient.SelectApplyAction(profile, state);
            if (action == ApplyAction.WaitForFn) { driverDetail.Text = FnDiagnosticGuidance(state); return; }
            bool finalApply = action != ApplyAction.DiagnoseFn;
            applying = true; apply.Enabled = false; UseWaitCursor = true; driverDetail.Text = "Applying… approve the Windows prompt. The keyboard reconnects for a moment.";
            await System.Threading.Tasks.Task.Run(delegate { DriverClient.ApplyElevated(profilePath, action); });
            if (finalApply) {
                DriverClient.SetHelperAutostart(profile.FKeyMode != 0);
                if (profile.FKeyMode != 0) DriverClient.StartMediaHelper();
            }
        } catch (Exception e) { applyError = "Apply did not finish: " + DriverClient.DescribeError(e); }
        finally { applying = false; if (!IsDisposed) { UseWaitCursor = false; RefreshDevice(); } }
    }
    private async void InstallDriver() {
        string script = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(Application.ExecutablePath), "..", "..", "scripts", "setup.ps1"));
        if (!File.Exists(script)) { MessageBox.Show(this, "setup.ps1 was not found next to this app:\r\n" + script, "Install driver"); return; }
        try {
            var start = new ProcessStartInfo(Path.Combine(Environment.SystemDirectory, @"WindowsPowerShell\v1.0\powershell.exe"),
                "-NoProfile -ExecutionPolicy RemoteSigned -File \"" + script + "\" -EnableTestSigning") { UseShellExecute = true, Verb = "runas" };
            install.Enabled = false; driverDetail.Text = "Setup is running in its own window…";
            using (var process = Process.Start(start)) await System.Threading.Tasks.Task.Run(() => process.WaitForExit());
        } catch (System.ComponentModel.Win32Exception e) { if (e.NativeErrorCode != 1223) MessageBox.Show(this, e.Message, "Install driver"); }
        if (IsDisposed) return;
        install.Enabled = true; RefreshDevice();
    }
    private void ShowMappings() {
        string text = "";
        for (int i = 0; i < MappingProfile.Sources.Length; ++i) {
            KeyTarget target = profile.Get(MappingProfile.Sources[i]);
            text += MappingProfile.SourceLabels[i] + "  →  " + (target == KeyTarget.Original ? "original" : MappingProfile.TargetLabels[(int)target]) + "\r\n";
        }
        text += "\r\nfn combinations: " + (profile.FnCombinations ? "delete → forward Delete; arrows → Home / End / Page Up / Page Down" : "off");
        text += "\r\nSwap exchanged keys: " + (profile.SwapExchangedKeys ? "on" : "off");
        text += "\r\nF-keys: " + fkeyMode.Text;
        text += "\r\n\r\nIcon functions: F1/F2 brightness, F3 Task View, F4 Search, F5 dictation, F6 sleep, F7/F8/F9 previous/play/next, F10/F11/F12 mute/volume.";
        MessageBox.Show(this, text, "All key mappings");
    }
    private void BuildMenuAndTray() {
        var menu = new MenuStrip { Dock = DockStyle.Top, BackColor = BackColor, Padding = new Padding(8, 2, 0, 2), RenderMode = ToolStripRenderMode.System };
        var file = new ToolStripMenuItem("&File");
        file.DropDownItems.Add(new ToolStripMenuItem("&Save profile", null, delegate { Save(); }) { ShortcutKeys = Keys.Control | Keys.S });
        file.DropDownItems.Add(new ToolStripMenuItem("&Apply to keyboard", null, delegate { if (apply.Enabled) Apply(); }) { ShortcutKeys = Keys.Control | Keys.Enter });
        file.DropDownItems.Add(new ToolStripSeparator());
        file.DropDownItems.Add(new ToolStripMenuItem("&Minimize to notification area", null, delegate { WindowState = FormWindowState.Minimized; }));
        file.DropDownItems.Add(new ToolStripMenuItem("E&xit", null, delegate { Close(); }));
        var options = new ToolStripMenuItem("&Options");
        startWithWindows = new ToolStripMenuItem("Start with &Windows") { CheckOnClick = true, Checked = AppSettings.StartWithWindows };
        startWithWindows.CheckedChanged += delegate {
            try { AppSettings.StartWithWindows = startWithWindows.Checked; }
            catch (Exception e) { MessageBox.Show(this, e.Message, "Start with Windows"); }
        };
        startMinimized = new ToolStripMenuItem("Start &minimized at sign-in") { CheckOnClick = true, Checked = AppSettings.StartMinimized,
            ToolTipText = "When Windows starts the app at sign-in, keep it in the notification area.\r\nClick the tray icon to open the window." };
        startMinimized.CheckedChanged += delegate {
            try { AppSettings.StartMinimized = startMinimized.Checked; if (startMinimized.Checked && !startWithWindows.Checked) startWithWindows.Checked = true; }
            catch (Exception e) { MessageBox.Show(this, e.Message, "Start minimized"); }
        };
        options.DropDownItems.Add(startWithWindows); options.DropDownItems.Add(startMinimized);
        var help = new ToolStripMenuItem("&Help");
        help.DropDownItems.Add(new ToolStripMenuItem("Driver &status…", null, delegate { ShowDriverStatus(); }));
        help.DropDownItems.Add(new ToolStripMenuItem("&About Magic Keyboard", null, delegate {
            MessageBox.Show(this, "Magic Keyboard 0.4.0.0\r\nOpenMagicKeyboard driver and app for the Apple Magic Keyboard A3203 (USB-C, 2024).\r\nMIT licensed.", "About Magic Keyboard");
        }));
        menu.Items.AddRange(new ToolStripItem[] { file, options, help });
        Controls.Add(menu); MainMenuStrip = menu;

        tray = new NotifyIcon { Icon = AppIcon.Get(16), Text = "Magic Keyboard", Visible = true };
        var trayMenu = new ContextMenuStrip();
        trayMenu.Items.Add("Open Magic Keyboard", null, delegate { ShowFromTray(); });
        trayMenu.Items.Add(new ToolStripSeparator());
        trayMenu.Items.Add("Exit", null, delegate { ShowFromTray(); Close(); });
        tray.ContextMenuStrip = trayMenu;
        tray.MouseClick += delegate(object s, MouseEventArgs e) { if (e.Button == MouseButtons.Left) ShowFromTray(); };
    }
    /// <summary>Started minimized: create the window (for messages) but keep it hidden until opened from the tray.</summary>
    protected override void SetVisibleCore(bool value) {
        if (value && startHidden && !allowShow) { if (!IsHandleCreated) CreateHandle(); value = false; }
        base.SetVisibleCore(value);
    }
    protected override void OnResize(EventArgs e) {
        base.OnResize(e);
        if (WindowState == FormWindowState.Minimized && tray != null && Visible) Hide(); // minimize to the notification area
    }
    protected override void WndProc(ref Message m) {
        if (SingleInstance.ShowMessage != 0 && m.Msg == SingleInstance.ShowMessage) { ShowFromTray(); return; }
        base.WndProc(ref m);
    }
    private void ShowFromTray() {
        allowShow = true;
        if (!Visible) Show();
        if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
        Activate(); RefreshDevice();
    }
    private void ShowDriverStatus() {
        string text;
        try { text = DriverClient.Describe(DriverClient.Read()); }
        catch (Exception e) { text = "Driver unavailable: " + DriverClient.DescribeError(e); }
        try { DriverHealth health = DeviceStatus.Diagnose(true); text = health.Summary + " — " + health.Detail + "\r\nTest signing: " + (health.TestSigning ? "on" : "off") + "\r\n\r\n" + text; } catch { }
        MessageBox.Show(this, text, "Driver status");
    }
    protected override void Dispose(bool disposing) {
        if (disposing) { refresh.Dispose(); tips.Dispose(); if (tray != null) { tray.Visible = false; tray.Dispose(); } }
        base.Dispose(disposing);
    }
}}
