[CmdletBinding()]
param(
    [string]$MasterRedPath,
    [string]$BlueDonorPath,
    [string]$DestinationDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($MasterRedPath)) {
    $MasterRedPath = Join-Path $PSScriptRoot '..\resources\tiga_chest_master_red.png'
}
if ([string]::IsNullOrWhiteSpace($BlueDonorPath)) {
    $BlueDonorPath = Join-Path $PSScriptRoot '..\resources\tiga_chest_blue_lens_donor.png'
}
if ([string]::IsNullOrWhiteSpace($DestinationDirectory)) {
    $DestinationDirectory = Join-Path $PSScriptRoot '..\resources'
}

# PowerShell 7's Store-delivered System.Drawing.Common facade cannot compile
# the required LockBits helper reliably. Windows PowerShell uses the native
# Windows System.Drawing assembly, so transparently run the generator there.
if ($PSVersionTable.PSEdition -eq 'Core') {
    $windowsPowerShell = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    & $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath `
        -MasterRedPath $MasterRedPath -BlueDonorPath $BlueDonorPath `
        -DestinationDirectory $DestinationDirectory
    exit $LASTEXITCODE
}

$resolvedMaster = (Resolve-Path -LiteralPath $MasterRedPath).Path
$resolvedDonor = (Resolve-Path -LiteralPath $BlueDonorPath).Path
$resolvedDestination = (Resolve-Path -LiteralPath $DestinationDirectory).Path

if (-not ('ThemeVariantGenerator' -as [type])) {
    Add-Type -AssemblyName System.Drawing
    Add-Type -Language CSharp -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

public sealed class ThemeVariantResult
{
    public int Width;
    public int Height;
    public int BlueChangedPixels;
    public int OffChangedPixels;
}

public static class ThemeVariantGenerator
{
    private const double ReferenceWidth = 1689.0;
    private const double ReferenceHeight = 931.0;

    private static byte Clamp(double value)
    {
        return (byte)Math.Round(Math.Max(0.0, Math.Min(255.0, value)));
    }

    private static byte Blend(byte original, byte replacement, double amount)
    {
        return Clamp(original * (1.0 - amount) + replacement * amount);
    }

    private static double SmoothStep(double value)
    {
        value = Math.Max(0.0, Math.Min(1.0, value));
        return value * value * (3.0 - 2.0 * value);
    }

    private static Bitmap LoadArgb(string path)
    {
        using (Bitmap source = new Bitmap(path))
        {
            Bitmap result = new Bitmap(source.Width, source.Height, PixelFormat.Format32bppArgb);
            using (Graphics graphics = Graphics.FromImage(result))
            {
                graphics.CompositingMode = CompositingMode.SourceCopy;
                graphics.DrawImage(source,
                    new Rectangle(0, 0, source.Width, source.Height),
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
        finally
        {
            bitmap.UnlockBits(data);
        }
    }

    private static void SaveBytes(byte[] bytes, int width, int height, int stride,
                                  string path)
    {
        using (Bitmap bitmap = new Bitmap(width, height, PixelFormat.Format32bppArgb))
        {
            Rectangle bounds = new Rectangle(0, 0, width, height);
            BitmapData data = bitmap.LockBits(bounds, ImageLockMode.WriteOnly,
                                              PixelFormat.Format32bppArgb);
            try
            {
                if (data.Stride != stride)
                    throw new InvalidOperationException("Unexpected bitmap stride.");
                Marshal.Copy(bytes, 0, data.Scan0, bytes.Length);
            }
            finally
            {
                bitmap.UnlockBits(data);
            }
            bitmap.Save(path, ImageFormat.Png);
        }
    }

    public static ThemeVariantResult Generate(string masterPath, string blueDonorPath,
                                               string destinationDirectory)
    {
        using (Bitmap master = LoadArgb(masterPath))
        using (Bitmap donor = LoadArgb(blueDonorPath))
        {
            if (master.Width != donor.Width || master.Height != donor.Height)
                throw new InvalidOperationException("Master and blue donor dimensions differ.");

            int stride;
            int donorStride;
            byte[] masterBytes = ReadBytes(master, out stride);
            byte[] donorBytes = ReadBytes(donor, out donorStride);
            if (stride != donorStride)
                throw new InvalidOperationException("Master and blue donor strides differ.");

            byte[] blueBytes = (byte[])masterBytes.Clone();
            byte[] offBytes = (byte[])masterBytes.Clone();
            byte[] stoneBytes = (byte[])masterBytes.Clone();

            double centerX = master.Width * (814.0 / ReferenceWidth);
            double centerY = master.Height * (332.5 / ReferenceHeight);
            double glassRadiusX = master.Width * (83.0 / ReferenceWidth);
            double glassRadiusY = master.Height * (72.0 / ReferenceHeight);
            double bluePatchRadiusX = master.Width * (106.0 / ReferenceWidth);
            double bluePatchRadiusY = master.Height * (96.0 / ReferenceHeight);
            int blueChanges = 0;
            int offChanges = 0;

            for (int y = 0; y < master.Height; ++y)
            {
                for (int x = 0; x < master.Width; ++x)
                {
                    int index = y * stride + x * 4;
                    byte sourceB = masterBytes[index + 0];
                    byte sourceG = masterBytes[index + 1];
                    byte sourceR = masterBytes[index + 2];
                    double luma = 0.2126 * sourceR + 0.7152 * sourceG + 0.0722 * sourceB;

                    // The entire master image loses colour, contrast and shine at 0%.
                    // A tiny deterministic grain prevents a flat grayscale-filter look.
                    double grain = ((x * 37 + y * 17 + x * y * 3) % 13) - 6;
                    double stoneBase = 52.0 + 0.53 * luma + 0.65 * grain;
                    stoneBytes[index + 0] = Clamp(stoneBase * 1.025);
                    stoneBytes[index + 1] = Clamp(stoneBase * 1.000);
                    stoneBytes[index + 2] = Clamp(stoneBase * 0.975);

                    double blueDx = (x - centerX) / bluePatchRadiusX;
                    double blueDy = (y - centerY) / bluePatchRadiusY;
                    double blueRadius = Math.Sqrt(blueDx * blueDx + blueDy * blueDy);
                    if (blueRadius <= 1.0)
                    {
                        // Copy the complete blue lens, its metal rim and the tiny
                        // matching reflection so no red fringe survives the edit.
                        double blueAmount = blueRadius <= 0.86
                            ? 1.0
                            : SmoothStep((1.0 - blueRadius) / 0.14);
                        for (int channel = 0; channel < 3; ++channel)
                            blueBytes[index + channel] = Blend(
                                masterBytes[index + channel], donorBytes[index + channel], blueAmount);
                        if (blueBytes[index + 0] != masterBytes[index + 0] ||
                            blueBytes[index + 1] != masterBytes[index + 1] ||
                            blueBytes[index + 2] != masterBytes[index + 2])
                            ++blueChanges;
                    }

                    double glassDx = (x - centerX) / glassRadiusX;
                    double glassDy = (y - centerY) / glassRadiusY;
                    double glassRadius = Math.Sqrt(glassDx * glassDx + glassDy * glassDy);
                    if (glassRadius <= 1.0)
                    {
                        // The blink's dark phase remains a recognisable wine-red
                        // glass lens and keeps most of the supplied swirl texture.
                        double edgeAmount = glassRadius <= 0.84
                            ? 1.0
                            : SmoothStep((1.0 - glassRadius) / 0.16);
                        double residual = Math.Max(0.0, 1.0 - glassRadius);
                        byte offR = Clamp(66.0 + 0.25 * luma + 18.0 * residual);
                        byte offG = Clamp(7.0 + 0.035 * luma + 3.0 * residual);
                        byte offB = Clamp(11.0 + 0.055 * luma + 4.0 * residual);
                        double offAmount = edgeAmount * 0.62;
                        offBytes[index + 2] = Blend(sourceR, offR, offAmount);
                        offBytes[index + 1] = Blend(sourceG, offG, offAmount);
                        offBytes[index + 0] = Blend(sourceB, offB, offAmount);
                        if (offBytes[index + 0] != sourceB ||
                            offBytes[index + 1] != sourceG ||
                            offBytes[index + 2] != sourceR)
                            ++offChanges;
                    }
                }
            }

            Directory.CreateDirectory(destinationDirectory);
            SaveBytes(blueBytes, master.Width, master.Height, stride,
                      Path.Combine(destinationDirectory, "tiga_chest_reference.png"));
            File.Copy(masterPath,
                      Path.Combine(destinationDirectory, "tiga_chest_warning_on.png"), true);
            SaveBytes(offBytes, master.Width, master.Height, stride,
                      Path.Combine(destinationDirectory, "tiga_chest_warning_off.png"));
            SaveBytes(stoneBytes, master.Width, master.Height, stride,
                      Path.Combine(destinationDirectory, "tiga_chest_stone.png"));

            return new ThemeVariantResult {
                Width = master.Width,
                Height = master.Height,
                BlueChangedPixels = blueChanges,
                OffChangedPixels = offChanges
            };
        }
    }
}
'@
}

$result = [ThemeVariantGenerator]::Generate(
    $resolvedMaster, $resolvedDonor, $resolvedDestination)

Write-Host ("Generated {0}x{1} exact-master theme variants. Blue lens pixels: {2}; dark red lens pixels: {3}." -f
    $result.Width, $result.Height, $result.BlueChangedPixels, $result.OffChangedPixels)
