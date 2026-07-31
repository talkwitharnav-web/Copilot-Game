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

# Client rect only: the title bar and border are not the thing being checked.
$rect = New-Object WinCap3+RECT
[void][WinCap3]::GetClientRect($handle, [ref]$rect)
$origin = New-Object WinCap3+POINT
[void][WinCap3]::ClientToScreen($handle, [ref]$origin)

$width = $rect.R - $rect.L
$height = $rect.B - $rect.T
$bitmap = New-Object System.Drawing.Bitmap($width, $height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($origin.X, $origin.Y, 0, 0, $bitmap.Size)
$graphics.Dispose()

$bitmap.Save((Join-Path (Get-Location) $Output), [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()

Write-Host "wrote $Output ($width x $height)"
