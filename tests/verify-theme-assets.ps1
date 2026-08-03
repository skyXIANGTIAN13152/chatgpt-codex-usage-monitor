[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$MasterRedPath,
    [Parameter(Mandatory = $true)][string]$SourcePath,
    [Parameter(Mandatory = $true)][string]$WarningOnPath,
    [Parameter(Mandatory = $true)][string]$WarningOffPath,
    [Parameter(Mandatory = $true)][string]$StonePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSEdition -eq 'Core') {
    $windowsPowerShell = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    & $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath `
        -MasterRedPath $MasterRedPath -SourcePath $SourcePath `
        -WarningOnPath $WarningOnPath -WarningOffPath $WarningOffPath `
        -StonePath $StonePath
    exit $LASTEXITCODE
}

Add-Type -AssemblyName System.Drawing
Add-Type -Language CSharp -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class ThemeAssetVerifier
{
    private static Bitmap LoadArgb(string path)
    {
        using (Bitmap source = new Bitmap(path))
        {
            Bitmap result = new Bitmap(source.Width, source.Height, PixelFormat.Format32bppArgb);
            using (Graphics graphics = Graphics.FromImage(result))
            {
                graphics.CompositingMode = CompositingMode.SourceCopy;
                graphics.DrawImage(source, new Rectangle(0, 0, source.Width, source.Height),
                                   0, 0, source.Width, source.Height, GraphicsUnit.Pixel);
            }
            return result;
        }
    }

    private static byte[] ReadBytes(Bitmap bitmap, out int stride)
    {
        Rectangle bounds = new Rectangle(0, 0, bitmap.Width, bitmap.Height);
        BitmapData data = bitmap.LockBits(bounds, ImageLockMode.ReadOnly,
                                          PixelFormat.Format32bppArgb);
        try
        {
            stride = data.Stride;
            byte[] bytes = new byte[Math.Abs(stride) * bitmap.Height];
            Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);
            return bytes;
        }
        finally { bitmap.UnlockBits(data); }
    }

    private static bool RgbDiffers(byte[] left, byte[] right, int index)
    {
        return left[index] != right[index] || left[index + 1] != right[index + 1] ||
               left[index + 2] != right[index + 2];
    }

    public static string Verify(string masterPath, string normalPath,
                                string warningOnPath, string warningOffPath,
                                string stonePath)
    {
        using (Bitmap master = LoadArgb(masterPath))
        using (Bitmap normal = LoadArgb(normalPath))
        using (Bitmap warningOn = LoadArgb(warningOnPath))
        using (Bitmap warningOff = LoadArgb(warningOffPath))
        using (Bitmap stone = LoadArgb(stonePath))
        {
            Bitmap[] candidates = { normal, warningOn, warningOff, stone };
            foreach (Bitmap candidate in candidates)
                if (candidate.Width != master.Width || candidate.Height != master.Height)
                    throw new InvalidOperationException("Theme image dimensions differ from master.");

            int masterStride, normalStride, onStride, offStride, stoneStride;
            byte[] masterBytes = ReadBytes(master, out masterStride);
            byte[] normalBytes = ReadBytes(normal, out normalStride);
            byte[] onBytes = ReadBytes(warningOn, out onStride);
            byte[] offBytes = ReadBytes(warningOff, out offStride);
            byte[] stoneBytes = ReadBytes(stone, out stoneStride);
            if (normalStride != masterStride || onStride != masterStride ||
                offStride != masterStride || stoneStride != masterStride)
                throw new InvalidOperationException("Theme image strides differ from master.");

            int localLeft = (int)Math.Floor(master.Width * (690.0 / 1689.0));
            int localRight = (int)Math.Ceiling(master.Width * (940.0 / 1689.0));
            int localTop = (int)Math.Floor(master.Height * (220.0 / 931.0));
            int localBottom = (int)Math.Ceiling(master.Height * (450.0 / 931.0));
            int normalInside = 0, normalOutside = 0;
            int offInside = 0, offOutside = 0;
            int warningOnMismatch = 0, stoneChanges = 0;
            double stoneSpread = 0.0;

            for (int y = 0; y < master.Height; ++y)
            {
                for (int x = 0; x < master.Width; ++x)
                {
                    int index = y * masterStride + x * 4;
                    bool inside = x >= localLeft && x <= localRight &&
                                  y >= localTop && y <= localBottom;
                    if (RgbDiffers(masterBytes, normalBytes, index))
                    {
                        if (inside) ++normalInside; else ++normalOutside;
                    }
                    if (RgbDiffers(masterBytes, offBytes, index))
                    {
                        if (inside) ++offInside; else ++offOutside;
                    }
                    if (RgbDiffers(masterBytes, onBytes, index)) ++warningOnMismatch;
                    if (RgbDiffers(masterBytes, stoneBytes, index)) ++stoneChanges;
                    if (masterBytes[index + 3] != normalBytes[index + 3] ||
                        masterBytes[index + 3] != onBytes[index + 3] ||
                        masterBytes[index + 3] != offBytes[index + 3] ||
                        masterBytes[index + 3] != stoneBytes[index + 3])
                        throw new InvalidOperationException("Theme image changed alpha.");

                    byte b = stoneBytes[index], g = stoneBytes[index + 1], r = stoneBytes[index + 2];
                    stoneSpread += Math.Max(r, Math.Max(g, b)) - Math.Min(r, Math.Min(g, b));
                }
            }

            int pixelCount = master.Width * master.Height;
            if (warningOnMismatch != 0)
                throw new InvalidOperationException("Warning-on image is not the exact supplied master.");
            if (normalOutside != 0 || offOutside != 0)
                throw new InvalidOperationException(String.Format(
                    "State edit escaped timer area (normal={0}, off={1}).", normalOutside, offOutside));
            if (normalInside < pixelCount * 0.005 || offInside < pixelCount * 0.005)
                throw new InvalidOperationException("Timer state edit changed too few pixels.");
            if (stoneChanges < pixelCount * 0.95)
                throw new InvalidOperationException("Stone state did not transform the full image.");
            double averageSpread = stoneSpread / pixelCount;
            if (averageSpread > 10.0)
                throw new InvalidOperationException("Stone state retained too much colour.");

            return String.Format(
                "exact red master; normal/off edits local ({0}/{1} pixels); stone spread {2:F3}",
                normalInside, offInside, averageSpread);
        }
    }
}
'@

$result = [ThemeAssetVerifier]::Verify(
    (Resolve-Path -LiteralPath $MasterRedPath).Path,
    (Resolve-Path -LiteralPath $SourcePath).Path,
    (Resolve-Path -LiteralPath $WarningOnPath).Path,
    (Resolve-Path -LiteralPath $WarningOffPath).Path,
    (Resolve-Path -LiteralPath $StonePath).Path)
Write-Host "Theme assets verified: $result."
