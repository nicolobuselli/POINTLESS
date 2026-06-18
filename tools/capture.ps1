param(
    [string]$Exe = "build\Desktop_Qt_6_11_1_MinGW_64_bit-Debug\ULTRA_Ditherer.exe",
    [string]$Out = "capture.png",
    [int]$WaitMs = 2500,
    [switch]$Reuse
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Drawing.Imaging;
public class Win {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
    public static void Aware() { try { SetProcessDpiAwarenessContext((IntPtr)(-4)); } catch {} } // PER_MONITOR_AWARE_V2
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool repaint);
    public static void Fit(IntPtr h) { ShowWindow(h, 9); MoveWindow(h, 0, 0, 1500, 2300, true); } // SW_RESTORE; tall so scroll content is fully captured
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public static IntPtr Find(uint target) {
        IntPtr found = IntPtr.Zero; int best = 0;
        EnumWindows((h, p) => {
            uint pid; GetWindowThreadProcessId(h, out pid);
            if (pid == target && IsWindowVisible(h)) {
                RECT r; GetWindowRect(h, out r);
                int area = (r.Right - r.Left) * (r.Bottom - r.Top);
                if (area > best) { best = area; found = h; }
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static Bitmap Capture(IntPtr h) {
        RECT r; GetWindowRect(h, out r);
        int w = r.Right - r.Left, hh = r.Bottom - r.Top;
        if (w <= 0 || hh <= 0) return null;
        Bitmap bmp = new Bitmap(w, hh, PixelFormat.Format32bppArgb);
        using (Graphics g = Graphics.FromImage(bmp)) {
            IntPtr hdc = g.GetHdc();
            PrintWindow(h, hdc, 2); // PW_RENDERFULLCONTENT
            g.ReleaseHdc(hdc);
        }
        return bmp;
    }
}
"@ -ReferencedAssemblies System.Drawing

[Win]::Aware()   # per-monitor DPI aware → PrintWindow captures true physical pixels

$proc = $null
if ($Reuse) {
    $proc = Get-Process -Name "ULTRA_Ditherer" -ErrorAction SilentlyContinue | Select-Object -First 1
}
if (-not $proc) {
    Stop-Process -Name "ULTRA_Ditherer" -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 300
    $proc = Start-Process -FilePath $Exe -PassThru
    Start-Sleep -Milliseconds $WaitMs
}
$proc.Refresh()
$h = [Win]::Find([uint32]$proc.Id)
if ($h -eq [IntPtr]::Zero) {
    Start-Sleep -Milliseconds 1500
    $h = [Win]::Find([uint32]$proc.Id)
}
[Win]::Fit($h)
Start-Sleep -Milliseconds 600
$bmp = [Win]::Capture($h)
if ($bmp) { $bmp.Save((Join-Path (Get-Location) $Out), [System.Drawing.Imaging.ImageFormat]::Png); Write-Output "saved $Out $($bmp.Width)x$($bmp.Height)" }
else { Write-Output "capture failed (handle=$h)" }
