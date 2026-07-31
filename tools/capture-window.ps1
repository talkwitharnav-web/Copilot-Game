# Screenshots the running game window so HUD work can actually be looked at
# rather than reasoned about. HUD depth and layout mistakes are silent, so
# "it built" is not evidence that it renders.
#
#   .\tools\capture-window.ps1 -Title "Voxel Game" -Output shot.png -Keys "{F5}" -DelaySeconds 6

param(
    [string]$Title = "Voxel Game",
    [string]$Output = "capture.png",
    [string]$Keys = "",
    [int]$DelaySeconds = 5
)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

if (-not ([System.Management.Automation.PSTypeName]'WinCap3').Type) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinCap3 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, IntPtr pid);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint from, uint to, bool attach);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
    [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr hWnd, int attr, out RECT r, int size);

    // GetWindowRect includes an invisible resize border and drop shadow, which
    // captures as a black margin. The DWM frame bounds are what is actually on
    // screen.
    public static RECT VisibleBounds(IntPtr hWnd) {
        RECT r;
        const int DWMWA_EXTENDED_FRAME_BOUNDS = 9;
        if (DwmGetWindowAttribute(hWnd, DWMWA_EXTENDED_FRAME_BOUNDS, out r, Marshal.SizeOf(typeof(RECT))) == 0) {
            return r;
        }
        GetWindowRect(hWnd, out r);
        return r;
    }
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT p);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }

    // Windows refuses foreground changes from a background process unless the
    // caller shares an input queue with whatever currently has focus.
    public static bool ForceForeground(IntPtr hWnd) {
        uint target = GetWindowThreadProcessId(hWnd, IntPtr.Zero);
        uint current = GetWindowThreadProcessId(GetForegroundWindow(), IntPtr.Zero);
        uint me = GetCurrentThreadId();
        AttachThreadInput(current, me, true);
        AttachThreadInput(target, me, true);
        ShowWindow(hWnd, 9 /* SW_RESTORE */);
        BringWindowToTop(hWnd);
        bool ok = SetForegroundWindow(hWnd);
        AttachThreadInput(target, me, false);
        AttachThreadInput(current, me, false);
        return ok;
    }
}
"@
}

Start-Sleep -Seconds $DelaySeconds

$proc = Get-Process | Where-Object { $_.MainWindowTitle -eq $Title } | Select-Object -First 1
if (-not $proc) {
    Write-Error "no window titled '$Title'"
    exit 1
}

$handle = $proc.MainWindowHandle

# Required for capture as well as for keys: CopyFromScreen reads whatever pixels
# are on screen, so a window that is behind the editor captures the editor.
$focused = $false
for ($attempt = 0; $attempt -lt 12; $attempt++) {
    [void][WinCap3]::ForceForeground($handle)
    Start-Sleep -Milliseconds 350
    if ([WinCap3]::GetForegroundWindow() -eq $handle) {
        $focused = $true
        break
    }
}

if (-not $focused) {
    Write-Error "could not bring '$Title' to the front; refusing to capture"
    exit 1
}

if ($Keys -ne "") {
    # Gaining focus can clear buffered input, so let the app settle first.
    Start-Sleep -Milliseconds 1200
    [System.Windows.Forms.SendKeys]::SendWait($Keys)
    Start-Sleep -Milliseconds 900
}

# Absolute screen coordinates directly. `GetClientRect` plus `ClientToScreen`
# is the tidier pair but silently leaves the origin at (0,0) when the conversion
# fails, which captures the top-left of the desktop instead of the window.
$rect = [WinCap3]::VisibleBounds($handle)

# A window hanging off the edge of the screen captures black where it is not
# there to be read, so it is pulled fully into view first.
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
if ($rect.L -lt $screen.X -or $rect.T -lt $screen.Y -or $rect.R -gt ($screen.X + $screen.Width) -or
    $rect.B -gt ($screen.Y + $screen.Height)) {
    # SWP_NOSIZE | SWP_NOZORDER
    [void][WinCap3]::SetWindowPos($handle, [IntPtr]::Zero, $screen.X + 20, $screen.Y + 20, 0, 0, 0x0001 -bor 0x0004)
    Start-Sleep -Milliseconds 500
    $rect = [WinCap3]::VisibleBounds($handle)
}

$width = $rect.R - $rect.L
$height = $rect.B - $rect.T
$bitmap = New-Object System.Drawing.Bitmap($width, $height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($rect.L, $rect.T, 0, 0, $bitmap.Size)
$graphics.Dispose()

$bitmap.Save((Join-Path (Get-Location) $Output), [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()

Write-Host "wrote $Output ($width x $height)"
