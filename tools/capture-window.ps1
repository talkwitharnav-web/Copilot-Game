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

# Versioned name: Add-Type cannot redefine a type, so editing this block would
# otherwise fail forever in an already-running shell.
if (-not ([System.Management.Automation.PSTypeName]'WinCap2').Type) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinCap2 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT p);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
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

# Windows refuses foreground changes from a background process, so this must be
# confirmed rather than assumed: an unverified SendKeys goes to whatever is
# focused instead, which means keystrokes land in the editor.
$focused = $false
for ($attempt = 0; $attempt -lt 10; $attempt++) {
    [void][WinCap2]::SetForegroundWindow($handle)
    Start-Sleep -Milliseconds 400
    if ([WinCap2]::GetForegroundWindow() -eq $handle) {
        $focused = $true
        break
    }
}

if (-not $focused) {
    Write-Error "could not focus '$Title'; refusing to send keys or capture"
    exit 1
}

if ($Keys -ne "") {
    # Gaining focus can clear buffered input, so let the app settle first.
    Start-Sleep -Milliseconds 1200
    [System.Windows.Forms.SendKeys]::SendWait($Keys)
    Start-Sleep -Milliseconds 900
}

# Client rect only: the title bar and border are not the thing being checked.
$rect = New-Object WinCap2+RECT
[void][WinCap2]::GetClientRect($handle, [ref]$rect)
$origin = New-Object WinCap2+POINT
[void][WinCap2]::ClientToScreen($handle, [ref]$origin)

$width = $rect.R - $rect.L
$height = $rect.B - $rect.T
$bitmap = New-Object System.Drawing.Bitmap($width, $height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($origin.X, $origin.Y, 0, 0, $bitmap.Size)
$graphics.Dispose()

$bitmap.Save((Join-Path (Get-Location) $Output), [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()

Write-Host "wrote $Output ($width x $height)"
