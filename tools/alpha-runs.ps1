# Prints each row's opaque runs for a texture, which is step 1 of the model
# procedure in TEXTURING.md - `analyze-alpha.ps1` finds connected islands, and
# touching nets merge into one island and mislead you. Runs never do.
#
#   powershell -NoProfile -File tools\alpha-runs.ps1 -Path <png>
#
# Reading the output (TEXTURING.md 14.1), for a box w wide, h tall, d deep with
# its net origin at (u, v):
#
#   band row (top + bottom rects):  width 2w      starting at x = u + d, for d rows
#   side row (all four sides):      width 2(w+d)  starting at x = u,     for h rows
#
# A band that measures HALF the predicted width means one of the two rects is
# transparent, and which one says where the box is buried.

param(
    [Parameter(Mandatory = $true)][string]$Path
)

Add-Type -AssemblyName System.Drawing

$bitmap = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Path).Path)
Write-Host ("{0}  {1}x{2}" -f (Split-Path -Leaf $Path), $bitmap.Width, $bitmap.Height)

for ($y = 0; $y -lt $bitmap.Height; $y++) {
    $runs = @()
    $start = -1
    for ($x = 0; $x -le $bitmap.Width; $x++) {
        $on = ($x -lt $bitmap.Width) -and ($bitmap.GetPixel($x, $y).A -ge 128)
        if ($on -and $start -lt 0) { $start = $x }
        if (-not $on -and $start -ge 0) {
            $runs += "{0}-{1}({2})" -f $start, ($x - 1), ($x - $start)
            $start = -1
        }
    }
    if ($runs.Count) { Write-Host ("{0,3}: {1}" -f $y, ($runs -join '  ')) }
}

$bitmap.Dispose()
