// SPDX-License-Identifier: MIT. Vector drawing of the compact Magic Keyboard (A3203)
// that highlights keys whose behavior the current profile changes.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace OpenMagicKeyboard.Gui
{
    internal sealed class KeyboardView : Control
    {
        private sealed class Key
        {
            internal RectangleF Box; internal string Top, Main, Id; internal bool Small, Glyph;
            internal PointF[] Shape; // non-rectangular (ISO Return)
        }
        private static readonly string[] FKeyGlyphs = { "", "", "", "", "", "", "", "", "", "", "", "" };
        private readonly List<Key> keys = new List<Key>();
        private MappingProfile profile = MappingProfile.Defaults();
        private bool iso;
        internal static readonly Color Accent = Color.FromArgb(46, 160, 67);

        internal KeyboardView()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw | ControlStyles.UserPaint, true);
            BackColor = Color.White; Build();
        }
        internal void Show(MappingProfile value)
        {
            profile = value ?? MappingProfile.Defaults();
            if (iso != profile.IsoLayout) { iso = profile.IsoLayout; Build(); }
            Invalidate();
        }
        private void Add(float x, float y, float w, float h, string main, string top = null, string id = null, bool small = false)
        {
            keys.Add(new Key { Box = new RectangleF(x, y, w, h), Main = main, Top = top, Id = id, Small = small });
        }
        private void Build()
        {
            keys.Clear();
            const float f = 0.62f; float y = 0;
            Add(0, y, 1.5f, f, "esc", null, null, true);
            for (int i = 0; i < 12; ++i) keys.Add(new Key { Box = new RectangleF(1.5f + i, y, 1, f), Main = FKeyGlyphs[i], Top = "F" + (i + 1), Id = "F" + (i + 1), Glyph = true });
            Add(13.5f, y, 1, f, "", null, null, true); keys[keys.Count - 1].Glyph = true;
            y = f + 0.08f;
            string[] numbers = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=" };
            string[] shifted = { "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "_", "+" };
            if (iso) Add(0, y, 1, 1, "§", "±", "Exchanged"); else Add(0, y, 1, 1, "`", "~");
            for (int i = 0; i < 12; ++i) Add(1 + i, y, 1, 1, numbers[i], shifted[i]);
            Add(13, y, 1.5f, 1, "delete", null, "Delete", true);
            y += 1;
            Add(0, y, 1.5f, 1, "tab", null, null, true);
            string row = "QWERTYUIOP";
            for (int i = 0; i < 10; ++i) Add(1.5f + i, y, 1, 1, row[i].ToString());
            Add(11.5f, y, 1, 1, "[", "{"); Add(12.5f, y, 1, 1, "]", "}");
            if (!iso) Add(13.5f, y, 1, 1, "\\", "|");
            float upper = y; y += 1;
            Add(0, y, 1.75f, 1, "caps lock", null, "CapsLock", true);
            row = "ASDFGHJKL";
            for (int i = 0; i < 9; ++i) Add(1.75f + i, y, 1, 1, row[i].ToString());
            Add(10.75f, y, 1, 1, ";", ":"); Add(11.75f, y, 1, 1, "'", "\"");
            if (iso) {
                Add(12.75f, y, 1, 1, "\\", "|");
                // ISO Return: 1u wide on the upper row, 0.75u on the home row.
                keys.Add(new Key { Main = "return", Small = true, Box = new RectangleF(13.5f, upper, 1, 2), Shape = new[] {
                    new PointF(13.5f, upper), new PointF(14.5f, upper), new PointF(14.5f, upper + 2), new PointF(13.75f, upper + 2),
                    new PointF(13.75f, upper + 1), new PointF(13.5f, upper + 1) } });
            } else Add(12.75f, y, 1.75f, 1, "return", null, null, true);
            y += 1;
            float x = 0;
            if (iso) { Add(0, y, 1.25f, 1, "shift", null, "LeftShift", true); Add(1.25f, y, 1, 1, "`", "~", "Exchanged"); x = 2.25f; }
            else { Add(0, y, 2.25f, 1, "shift", null, "LeftShift", true); x = 2.25f; }
            row = "ZXCVBNM";
            for (int i = 0; i < 7; ++i) Add(x + i, y, 1, 1, row[i].ToString());
            Add(x + 7, y, 1, 1, ",", "<"); Add(x + 8, y, 1, 1, ".", ">"); Add(x + 9, y, 1, 1, "/", "?");
            Add(x + 10, y, 2.25f, 1, "shift", null, "RightShift", true);
            y += 1;
            Add(0, y, 1, 1, "fn", "", "Fn", true);
            Add(1, y, 1, 1, "control", "^", "LeftCtrl", true);
            Add(2, y, 1, 1, "option", "⌥", "LeftAlt", true);
            Add(3, y, 1.25f, 1, "command", "⌘", "LeftWindows", true);
            Add(4.25f, y, 5, 1, "");
            Add(9.25f, y, 1.25f, 1, "command", "⌘", "RightWindows", true);
            Add(10.5f, y, 1, 1, "option", "⌥", "RightAlt", true);
            Add(11.5f, y + 0.5f, 1, 0.5f, "◀", null, "Arrow", true);
            Add(12.5f, y, 1, 0.5f, "▲", null, "Arrow", true);
            Add(12.5f, y + 0.5f, 1, 0.5f, "▼", null, "Arrow", true);
            Add(13.5f, y + 0.5f, 1, 0.5f, "▶", null, "Arrow", true);
        }
        private bool Changed(string id, out string target)
        {
            target = null;
            if (id == null) return false;
            if (id.StartsWith("F", StringComparison.Ordinal) && id.Length <= 3 && id != "Fn") return profile.FKeyMode != 0;
            if (id == "Exchanged") return profile.SwapExchangedKeys;
            if (id == "Delete" || id == "Arrow") return profile.FnCombinations;
            if (Array.IndexOf(MappingProfile.Sources, id) < 0) return false;
            KeyTarget mapped = profile.Get(id);
            if (mapped == KeyTarget.Original) return false;
            target = MappingProfile.ShortLabels[(int)mapped];
            return true;
        }
        private static GraphicsPath Rounded(RectangleF r, float radius)
        {
            var path = new GraphicsPath(); float d = Math.Min(radius * 2, Math.Min(r.Width, r.Height));
            path.AddArc(r.X, r.Y, d, d, 180, 90); path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90); path.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            path.CloseFigure(); return path;
        }
        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics; g.SmoothingMode = SmoothingMode.AntiAlias; g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.ClearTypeGridFit;
            g.Clear(BackColor);
            const float widthUnits = 14.5f, heightUnits = 5.7f, pad = 0.45f;
            float unit = Math.Min((Width - 2) / (widthUnits + 2 * pad), (Height - 2) / (heightUnits + 2 * pad));
            if (unit < 4) return;
            float ox = (Width - (widthUnits + 2 * pad) * unit) / 2, oy = (Height - (heightUnits + 2 * pad) * unit) / 2;
            var body = new RectangleF(ox, oy, (widthUnits + 2 * pad) * unit, (heightUnits + 2 * pad) * unit);
            using (var shadow = Rounded(new RectangleF(body.X + 2, body.Y + 3, body.Width, body.Height), unit * 0.35f))
            using (var shadowBrush = new SolidBrush(Color.FromArgb(28, 0, 0, 0))) g.FillPath(shadowBrush, shadow);
            using (var path = Rounded(body, unit * 0.35f))
            using (var fill = new LinearGradientBrush(body, Color.FromArgb(250, 250, 251), Color.FromArgb(232, 234, 237), 90f))
            using (var pen = new Pen(Color.FromArgb(190, 194, 200))) { g.FillPath(fill, path); g.DrawPath(pen, path); }
            float gap = unit * 0.09f, x0 = ox + pad * unit, y0 = oy + pad * unit;
            using (var legend = new Font("Segoe UI", Math.Max(6f, unit * 0.27f), GraphicsUnit.Pixel))
            using (var small = new Font("Segoe UI", Math.Max(5f, unit * 0.2f), GraphicsUnit.Pixel))
            using (var glyph = new Font("Segoe MDL2 Assets", Math.Max(5f, unit * 0.2f), GraphicsUnit.Pixel))
            using (var mapped = new Font("Segoe UI Semibold", Math.Max(5f, unit * 0.22f), GraphicsUnit.Pixel))
            using (var text = new SolidBrush(Color.FromArgb(60, 64, 70)))
            using (var faint = new SolidBrush(Color.FromArgb(120, 126, 134)))
            using (var accentBrush = new SolidBrush(Accent))
            using (var keyPen = new Pen(Color.FromArgb(205, 209, 214)))
            using (var accentPen = new Pen(Accent, Math.Max(1.5f, unit * 0.06f)))
            using (var keyFill = new SolidBrush(Color.White))
            using (var accentFill = new SolidBrush(Color.FromArgb(232, 246, 235))) {
                var center = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
                foreach (Key key in keys) {
                    string target; bool changed = Changed(key.Id, out target);
                    GraphicsPath shape;
                    if (key.Shape != null) {
                        var points = new PointF[key.Shape.Length];
                        for (int i = 0; i < points.Length; ++i) points[i] = new PointF(x0 + key.Shape[i].X * unit + (i == 0 || i == 5 || i == 3 || i == 4 ? gap / 2 : -gap / 2), y0 + key.Shape[i].Y * unit + (i <= 1 ? gap / 2 : -gap / 2));
                        shape = new GraphicsPath(); shape.AddPolygon(points);
                    } else {
                        var r = new RectangleF(x0 + key.Box.X * unit + gap / 2, y0 + key.Box.Y * unit + gap / 2, key.Box.Width * unit - gap, key.Box.Height * unit - gap);
                        shape = Rounded(r, unit * 0.09f);
                    }
                    using (shape) {
                        g.FillPath(changed ? accentFill : keyFill, shape);
                        g.DrawPath(changed ? accentPen : keyPen, shape);
                    }
                    var box = new RectangleF(x0 + key.Box.X * unit + gap, y0 + key.Box.Y * unit + gap, key.Box.Width * unit - 2 * gap, key.Box.Height * unit - 2 * gap);
                    if (key.Glyph) {
                        var top = new RectangleF(box.X, box.Y, box.Width, box.Height * 0.62f);
                        g.DrawString(key.Main, glyph, text, top, center);
                        if (key.Top != null) g.DrawString(key.Top, small, faint, new RectangleF(box.X, box.Y + box.Height * 0.5f, box.Width, box.Height * 0.5f), center);
                    } else if (key.Small) {
                        var lower = new StringFormat { Alignment = key.Box.Width > 1.4f ? StringAlignment.Near : StringAlignment.Center, LineAlignment = StringAlignment.Far };
                        if (key.Top != null) g.DrawString(key.Top, key.Top.Length == 1 && key.Top[0] > '' ? glyph : small, faint, new RectangleF(box.X, box.Y, box.Width, box.Height * 0.5f), center);
                        g.DrawString(changed && target != null ? target : key.Main, changed && target != null ? mapped : small, changed && target != null ? accentBrush : text, box, key.Box.Height < 1 ? center : lower);
                    } else if (key.Top != null) {
                        g.DrawString(key.Top, small, faint, new RectangleF(box.X, box.Y, box.Width, box.Height * 0.48f), center);
                        g.DrawString(key.Main, legend, text, new RectangleF(box.X, box.Y + box.Height * 0.42f, box.Width, box.Height * 0.58f), center);
                    } else g.DrawString(key.Main, legend, text, box, center);
                }
            }
        }
    }
}
