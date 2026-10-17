// SPDX-License-Identifier: MIT. Read only the driver's opt-in Fn counters.
using System;
using System.Drawing;
using System.IO;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Windows.Forms;

namespace OpenMagicKeyboard.Gui
{
    [DataContract]
    internal sealed class FnTestObservation
    {
        [DataMember] public int SchemaVersion = 1;
        [DataMember] public string TimestampUtc, InstanceId;
        [DataMember] public uint BaselineDown, BaselineUp, FinalDown, FinalUp;
        [DataMember] public bool ThreeMatchedTaps;
    }
    internal sealed class FnTestWindow : Form
    {
        private readonly string outputPath;
        private readonly Timer poll = new Timer();
        private readonly Label status = new Label();
        private readonly Button start = new Button(), finish = new Button();
        private DriverState baseline;
        private bool matched;

        internal FnTestWindow(string path)
        {
            outputPath = Path.GetFullPath(path);
            if (File.Exists(outputPath) || !Directory.Exists(Path.GetDirectoryName(outputPath)))
                throw new IOException("Choose a new observation filename in an existing directory.");
            Text = "Apple Fn/Globe: confirm three taps";
            ClientSize = new Size(620, 245); StartPosition = FormStartPosition.CenterScreen;
            Font = new Font("Segoe UI", 10F);
            var instructions = new Label { Left = 18, Top = 18, Width = 585, Height = 105,
                Text = "Release all keys and click Start. Then tap and release Fn/Globe on the Apple keyboard exactly three times. Press no other keys during this test.\r\n\r\nThis window reads only Fn counters. It does not assign an action to Globe or record ordinary input." };
            status.SetBounds(18, 130, 585, 55);
            start.Text = "Start / reset"; start.SetBounds(18, 195, 140, 32);
            finish.Text = "Finish"; finish.SetBounds(455, 195, 140, 32); finish.Enabled = false;
            start.Click += delegate {
                try {
                    DriverState state = ReadDiagnostic();
                    if (state.FnDown != state.FnUp) throw new InvalidOperationException("Release Fn/Globe before starting; its counters currently show a held key.");
                    baseline = state; RefreshCounts();
                } catch (Exception error) { baseline = null; matched = false; finish.Enabled = false; status.Text = error.Message; }
            };
            finish.Click += delegate {
                RefreshCounts(); if (!matched || baseline == null) return;
                try {
                    DriverState state = ReadDiagnostic();
                    if (!Matches(state)) { RefreshCounts(); return; }
                    var observation = new FnTestObservation { TimestampUtc = DateTime.UtcNow.ToString("o"),
                        InstanceId = state.InstanceId, BaselineDown = baseline.FnDown, BaselineUp = baseline.FnUp,
                        FinalDown = state.FnDown, FinalUp = state.FnUp, ThreeMatchedTaps = true };
                    using (var file = new FileStream(outputPath, FileMode.CreateNew, FileAccess.Write, FileShare.None)) {
                        new DataContractJsonSerializer(typeof(FnTestObservation)).WriteObject(file, observation); file.Flush(true);
                    }
                    DialogResult = DialogResult.OK; Close();
                } catch (Exception error) { MessageBox.Show(this, error.Message, "Fn observation failed", MessageBoxButtons.OK, MessageBoxIcon.Error); }
            };
            Controls.Add(instructions); Controls.Add(status); Controls.Add(start); Controls.Add(finish);
            poll.Interval = 250; poll.Tick += delegate { RefreshCounts(); };
            Shown += delegate { RefreshCounts(); poll.Start(); };
        }
        private static DriverState ReadDiagnostic()
        {
            DriverState state = DriverClient.Read();
            if (!state.LayoutValid || !state.FnAvailable || state.ActiveFlags != 2 || state.PendingFlags != 2)
                throw new InvalidOperationException("Enable the Fn-only diagnostic policy before starting this test.");
            return state;
        }
        private bool Matches(DriverState state)
        {
            return baseline != null && state.InstanceId == baseline.InstanceId &&
                state.ReportId == baseline.ReportId && state.ReportBytes == baseline.ReportBytes &&
                state.FnDown >= baseline.FnDown && state.FnUp >= baseline.FnUp &&
                state.FnDown - baseline.FnDown == 3 && state.FnUp - baseline.FnUp == 3;
        }
        private void RefreshCounts()
        {
            matched = false;
            try {
                DriverState state = ReadDiagnostic();
                if (baseline == null) status.Text = "Ready. Release all keys, then click Start.";
                else if (state.InstanceId != baseline.InstanceId || state.ReportId != baseline.ReportId ||
                         state.ReportBytes != baseline.ReportBytes || state.FnDown < baseline.FnDown || state.FnUp < baseline.FnUp) {
                    baseline = null; status.Text = "Device or counters changed. Release all keys and restart the test.";
                } else {
                    uint down = state.FnDown - baseline.FnDown, up = state.FnUp - baseline.FnUp;
                    matched = Matches(state);
                    status.Text = "Fn down/up since Start: " + down + " / " + up + "\r\n" +
                        (matched ? "Three matched taps detected. Click Finish." : down > 3 || up > 3 ? "Extra transitions detected. Click Start to reset and repeat three taps." : "Tap and release Fn/Globe until the counts are 3 / 3.");
                }
            } catch (Exception error) { baseline = null; status.Text = error.Message; }
            finish.Enabled = matched;
        }
        protected override void Dispose(bool disposing) { if (disposing) poll.Dispose(); base.Dispose(disposing); }
    }
}
