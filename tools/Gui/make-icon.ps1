# SPDX-License-Identifier: MIT
# Renders tools\Gui\app.ico: a Magic Keyboard style icon (silver aluminium body,
# white keys) at 16-256 px. Original artwork; no Apple logo. Run once after
# changing the design; build-gui.ps1 embeds the committed app.ico.
[CmdletBinding()]
param([string]$OutputPath = (Join-Path $PSScriptRoot 'app.ico'),[string]$PreviewPath)
$ErrorActionPreference = 'Stop'
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
public static class MagicIcon {
    static GraphicsPath Round(RectangleF r, float radius) {
        var p = new GraphicsPath(); float d = Math.Min(radius * 2, Math.Min(r.Width, r.Height));
        if (d < 0.5f) { p.AddRectangle(r); return p; }
        p.AddArc(r.X, r.Y, d, d, 180, 90); p.AddArc(r.Right - d, r.Y, d, d, 270, 90);
        p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90); p.AddArc(r.X, r.Bottom - d, d, d, 90, 90); p.CloseFigure(); return p;
    }
    static void Key(Graphics g, RectangleF r, float s, bool detailed) {
        using (var path = Round(r, Math.Max(0.6f, r.Height * 0.18f))) {
            if (detailed) using (var shade = new SolidBrush(Color.FromArgb(70, 60, 66, 74))) { var m = new Matrix(); m.Translate(0, Math.Max(0.6f, s / 160f)); using (var sp = (GraphicsPath)path.Clone()) { sp.Transform(m); g.FillPath(shade, sp); } }
            using (var fill = new LinearGradientBrush(new RectangleF(r.X, r.Y - 1, r.Width, r.Height + 2), Color.White, Color.FromArgb(240, 242, 245), 90f)) g.FillPath(fill, path);
            if (detailed) using (var pen = new Pen(Color.FromArgb(150, 176, 181, 188), Math.Max(0.5f, s / 256f))) g.DrawPath(pen, path);
        }
    }
    public static Bitmap Render(int s) {
        var bmp = new Bitmap(s, s, PixelFormat.Format32bppArgb);
        using (var g = Graphics.FromImage(bmp)) {
            g.SmoothingMode = SmoothingMode.AntiAlias; g.PixelOffsetMode = PixelOffsetMode.HighQuality; g.Clear(Color.Transparent);
            bool small = s < 32;
            float bodyH = s * (small ? 0.62f : 0.5f);
            var body = new RectangleF(s * 0.03f, (s - bodyH) / 2f, s * 0.94f, bodyH);
            // Soft shadow under the body.
            if (!small) using (var shadow = Round(new RectangleF(body.X + s * 0.01f, body.Y + s * 0.035f, body.Width, body.Height), s * 0.07f))
                using (var brush = new SolidBrush(Color.FromArgb(55, 0, 0, 0))) g.FillPath(brush, shadow);
            using (var path = Round(body, s * (small ? 0.09f : 0.07f))) {
                using (var fill = new LinearGradientBrush(body, Color.FromArgb(246, 247, 249), Color.FromArgb(196, 201, 208), 90f)) g.FillPath(fill, path);
                using (var pen = new Pen(Color.FromArgb(120, 127, 136), Math.Max(1f, s / 96f))) g.DrawPath(pen, path);
            }
            float inset = s * (small ? 0.09f : 0.05f);
            var inner = new RectangleF(body.X + inset, body.Y + inset, body.Width - 2 * inset, body.Height - 2 * inset);
            if (small) {
                // 16-24 px: three key rows and a space bar read better than real keys.
                float rowH = inner.Height / 4f;
                for (int row = 0; row < 3; ++row) {
                    int keys = s >= 24 ? 7 : 4; float gap = Math.Max(1f, s / 24f), w = (inner.Width - gap * (keys - 1)) / keys;
                    for (int k = 0; k < keys; ++k) Key(g, new RectangleF(inner.X + k * (w + gap), inner.Y + row * rowH, w, rowH - gap), s, false);
                }
                Key(g, new RectangleF(inner.X + inner.Width * 0.25f, inner.Y + 3 * rowH, inner.Width * 0.5f, rowH - Math.Max(1f, s / 24f)), s, false);
                return bmp;
            }
            // Real compact layout in key units: 14.5 wide, F-row 0.6 + five rows.
            float u = inner.Width / 14.5f, v = inner.Height / 5.6f, gp = Math.Max(1f, u * 0.12f);
            Action<float, float, float, float> add = (x, y, w, h) => Key(g, new RectangleF(inner.X + x * u + gp / 2, inner.Y + y * v + gp / 2, w * u - gp, h * v - gp), s, s >= 48);
            add(0, 0, 1.5f, 0.6f); for (int i = 0; i < 13; ++i) add(1.5f + i, 0, 1, 0.6f);
            float y0 = 0.6f;
            for (int i = 0; i < 13; ++i) add(i, y0, 1, 1); add(13, y0, 1.5f, 1);
            add(0, y0 + 1, 1.5f, 1); for (int i = 0; i < 13; ++i) add(1.5f + i, y0 + 1, 1, 1);
            add(0, y0 + 2, 1.75f, 1); for (int i = 0; i < 11; ++i) add(1.75f + i, y0 + 2, 1, 1); add(12.75f, y0 + 2, 1.75f, 1);
            add(0, y0 + 3, 2.25f, 1); for (int i = 0; i < 10; ++i) add(2.25f + i, y0 + 3, 1, 1); add(12.25f, y0 + 3, 2.25f, 1);
            add(0, y0 + 4, 1, 1); add(1, y0 + 4, 1, 1); add(2, y0 + 4, 1, 1); add(3, y0 + 4, 1.25f, 1); add(4.25f, y0 + 4, 5, 1);
            add(9.25f, y0 + 4, 1.25f, 1); add(10.5f, y0 + 4, 1, 1);
            add(11.5f, y0 + 4.5f, 1, 0.5f); add(12.5f, y0 + 4, 1, 0.5f); add(12.5f, y0 + 4.5f, 1, 0.5f); add(13.5f, y0 + 4.5f, 1, 0.5f);
        }
        return bmp;
    }
    static byte[] Dib(Bitmap b) {
        int s = b.Width, mask = ((s + 31) / 32) * 4 * s;
        using (var m = new MemoryStream()) using (var w = new BinaryWriter(m)) {
            w.Write(40); w.Write(s); w.Write(s * 2); w.Write((short)1); w.Write((short)32); w.Write(0); w.Write(s * s * 4 + mask); w.Write(0); w.Write(0); w.Write(0); w.Write(0);
            for (int y = s - 1; y >= 0; --y) for (int x = 0; x < s; ++x) { Color c = b.GetPixel(x, y); w.Write(c.B); w.Write(c.G); w.Write(c.R); w.Write(c.A); }
            w.Write(new byte[mask]); return m.ToArray();
        }
    }
    public static void Save(string path, int[] sizes) {
        var images = new byte[sizes.Length][];
        for (int i = 0; i < sizes.Length; ++i) using (var b = Render(sizes[i])) {
            if (sizes[i] >= 256) using (var m = new MemoryStream()) { b.Save(m, ImageFormat.Png); images[i] = m.ToArray(); }
            else images[i] = Dib(b);
        }
        using (var f = new FileStream(path, FileMode.Create)) using (var w = new BinaryWriter(f)) {
            w.Write((short)0); w.Write((short)1); w.Write((short)sizes.Length);
            int offset = 6 + 16 * sizes.Length;
            for (int i = 0; i < sizes.Length; ++i) {
                w.Write((byte)(sizes[i] >= 256 ? 0 : sizes[i])); w.Write((byte)(sizes[i] >= 256 ? 0 : sizes[i])); w.Write((byte)0); w.Write((byte)0);
                w.Write((short)1); w.Write((short)32); w.Write(images[i].Length); w.Write(offset); offset += images[i].Length;
            }
            foreach (var img in images) w.Write(img);
        }
    }
}
'@
[MagicIcon]::Save($OutputPath, [int[]](256, 64, 48, 32, 24, 20, 16))
if ($PreviewPath) { $bmp = [MagicIcon]::Render(256); $bmp.Save($PreviewPath, [Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose() }
Write-Host "Icon: $OutputPath"
