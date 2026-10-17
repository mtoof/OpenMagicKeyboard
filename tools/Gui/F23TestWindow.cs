// SPDX-License-Identifier: MIT. Focused F23 messages only; no hooks or text capture.
using System;
using System.Drawing;
using System.IO;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Windows.Forms;
namespace OpenMagicKeyboard.Gui
{
    [DataContract]
    internal sealed class F23Observation
    {
        [DataMember] public int SchemaVersion = 1;
        [DataMember] public string TimestampUtc;
        [DataMember] public int Down, Up, Repeats, OrphanUp, Interrupted;
        [DataMember] public bool Held, Finished, ThreeMatchedTaps;
    }
    internal sealed class F23TestWindow : Form
    {
        private readonly Label counts = new Label();
        private readonly F23Observation observation = new F23Observation();
        private readonly Button finish = new Button();
        private readonly string outputPath;
        internal F23TestWindow(string path)
        {
            outputPath = Path.GetFullPath(path);
            if (File.Exists(outputPath) || !Directory.Exists(Path.GetDirectoryName(outputPath)))
                throw new IOException("Choose a new observation filename in an existing directory.");
            Text = "Apple Fn/Globe: F23 test"; ClientSize = new Size(620, 245);
            StartPosition = FormStartPosition.CenterScreen; KeyPreview = true;
            Font = new Font("Segoe UI", 10F);
            var instruction = new Label { Left = 18, Top = 18, Width = 585, Height = 100,
                Text = "Release all keys and click Start / reset. Keep this window focused and quickly tap and release Fn/Globe on the Apple keyboard exactly three times.\r\nOnly F23 is counted. Ordinary keys are never recorded. Click Finish at 3 / 3." };
            counts.SetBounds(18, 125, 585, 65);
            finish.SetBounds(455, 195, 140, 32); finish.Text = "Finish";
            var start = new Button { Left = 18, Top = 195, Width = 140, Height = 32, Text = "Start / reset" };
            start.Click += delegate {
                if (observation.Held) return;
                observation.Down = observation.Up = observation.Repeats = observation.OrphanUp = observation.Interrupted = 0;
                observation.Finished = false; UpdateCounts();
            };
            finish.Click += delegate { observation.Finished = true; Close(); };
            Controls.Add(instruction); Controls.Add(counts); Controls.Add(start); Controls.Add(finish);
            KeyDown += delegate(object sender, KeyEventArgs e) {
                if (e.KeyCode != Keys.F23) return;
                if (observation.Held) ++observation.Repeats; else { observation.Held = true; ++observation.Down; }
                e.Handled = true; e.SuppressKeyPress = true; UpdateCounts();
            };
            KeyUp += delegate(object sender, KeyEventArgs e) {
                if (e.KeyCode != Keys.F23) return;
                if (observation.Held) { observation.Held = false; ++observation.Up; } else ++observation.OrphanUp;
                e.Handled = true; UpdateCounts();
            };
            Deactivate += delegate { if (observation.Held) { ++observation.Interrupted; observation.Held = false; UpdateCounts(); } };
            Shown += delegate { Activate(); };
            FormClosing += delegate(object sender, FormClosingEventArgs e) {
                try {
                    observation.TimestampUtc = DateTime.UtcNow.ToString("o");
                    observation.ThreeMatchedTaps = observation.Finished && observation.Down == 3 && observation.Up == 3 &&
                        observation.Repeats == 0 && observation.OrphanUp == 0 && observation.Interrupted == 0 && !observation.Held;
                    using (var file = new FileStream(outputPath, FileMode.CreateNew, FileAccess.Write, FileShare.None)) {
                        new DataContractJsonSerializer(typeof(F23Observation)).WriteObject(file, observation); file.Flush(true);
                    }
                    DialogResult = observation.ThreeMatchedTaps ? DialogResult.OK : DialogResult.Cancel;
                } catch (Exception error) { e.Cancel = true; MessageBox.Show(this, error.Message, "F23 observation failed", MessageBoxButtons.OK, MessageBoxIcon.Error); }
            };
            UpdateCounts();
        }
        private void UpdateCounts()
        {
            finish.Enabled = observation.Down == 3 && observation.Up == 3 && observation.Repeats == 0 &&
                observation.OrphanUp == 0 && observation.Interrupted == 0 && !observation.Held;
            counts.Text = String.Format("F23 down/up: {0} / {1}; held: {2}\r\nRepeats: {3}; orphan releases: {4}; interrupted holds: {5}",
                observation.Down, observation.Up, observation.Held, observation.Repeats, observation.OrphanUp, observation.Interrupted);
        }
    }
}
